#include "FerryReceiver.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStorageInfo>
#include <QThread>
#include <QTimer>
#include <QWebSocket>

#include <cstring>

#include "core/HashList.h"
#include "core/Protocol.h"

namespace {

constexpr int kHeader = 1 + 8;                    // [op][индекс BE]
constexpr int kMaxBadChunks = 64;                 // подряд — дальше чинить нечего
constexpr qint64 kSeedHighWater = 2 * 1024 * 1024;
constexpr int kMaxRanges = 4096;                  // длиннее релей отвергает
constexpr quint64 kHashSegmentMax = 8192;
constexpr int kMapCells = 48;                     // клеток в карте тома карточки
constexpr qint64 kHelloTimeoutMs = 20000;
// Две минуты: на медленном канале чанк в 4 МиБ честно идёт десятки секунд,
// а сообщение приходит только целиком. Ложная тревога стоит дорого — новое
// подключение без токена возврата списало бы использование ссылки.
constexpr qint64 kStallMs = 120000;
constexpr int kReconnectSteps[] = {1, 2, 4, 8, 15, 30};

QString b64url(const QByteArray &raw)
{
    return QString::fromLatin1(
        raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool fromB64url(const QString &text, QByteArray &out)
{
    const auto r = QByteArray::fromBase64Encoding(
        text.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals
                             | QByteArray::AbortOnBase64DecodingErrors);
    if (!r)
        return false;
    out = r.decoded;
    return true;
}

QJsonArray rangesJson(const std::vector<ferry::ChunkRange> &ranges)
{
    QJsonArray a;
    const size_t n = std::min<size_t>(ranges.size(), kMaxRanges);
    for (size_t i = 0; i < n; ++i)
        a.append(QJsonArray{double(ranges[i].from), double(ranges[i].to)});
    return a;
}

// Свободное имя в папке: «том», «том (2)», «том (3)»… У файла номер встаёт
// перед расширением — так делает и проводник.
QString uniquePath(const QString &dir, const QString &name, bool isDir)
{
    QString base = name;
    QString ext;
    if (!isDir) {
        const int dot = name.lastIndexOf(QLatin1Char('.'));
        if (dot > 0) {
            base = name.left(dot);
            ext = name.mid(dot);
        }
    }
    QString candidate = dir + QLatin1Char('/') + name;
    for (int i = 2; QFileInfo::exists(candidate) && i < 10000; ++i)
        candidate = dir + QLatin1Char('/') + base + QStringLiteral(" (%1)").arg(i) + ext;
    return candidate;
}

} // namespace

void FerryReceiver::RateMeter::add(qint64 n, qint64 t)
{
    if (!last) {
        last = t;
        bytes = n;
        return;
    }
    bytes += n;
    const qint64 dt = t - last;
    if (dt >= 500) {
        const double inst = double(bytes) * 1000.0 / double(dt);
        value = value <= 0 ? inst : value * 0.7 + inst * 0.3;
        bytes = 0;
        last = t;
    }
}

// Тишина дольше двух секунд — скорость ноль, а не «последняя известная»:
// иначе застрявший приём показывал бы бодрые мегабайты.
double FerryReceiver::RateMeter::current(qint64 t) const
{
    return last && t - last > 2000 ? 0.0 : value;
}

FerryReceiver::FerryReceiver(Setup setup, QObject *parent)
    : QObject(parent), m_s(std::move(setup))
{
    m_seeding = m_s.seeding;
}

FerryReceiver::~FerryReceiver()
{
    if (m_ws) {
        m_ws->disconnect(this);
        m_ws->abort();
    }
}

const uint8_t *FerryReceiver::prefix() const
{
    return reinterpret_cast<const uint8_t *>(m_s.meta.noncePrefix.constData());
}

// ---------- запуск ----------

void FerryReceiver::start()
{
    m_clock.start();
    m_nam = new QNetworkAccessManager(this);
    m_tick = new QTimer(this);
    m_tick->setInterval(100);
    connect(m_tick, &QTimer::timeout, this, &FerryReceiver::housekeeping);
    m_reconnect = new QTimer(this);
    m_reconnect->setSingleShot(true);
    connect(m_reconnect, &QTimer::timeout, this, &FerryReceiver::connectRelay);

    const quint64 n = count();
    if (!m_s.volume.hashes.empty()) {
        m_hashes = m_s.volume.hashes;
        m_hashed = n;
    } else {
        m_hashes.assign(size_t(n) * 32, 0);
        m_hashed = 0;
    }
    m_hashesComplete = !m_s.meta.stream;
    m_have.reset(n);
    m_pending.reset(n);
    m_late.reset(n);
    m_serveQueue.reset(n);

    const bool tree = m_s.volume.manifest.isTree();
    QString err;
    if (!m_s.finalPath.isEmpty()) {
        // Том собран в прошлый раз и лежит на месте: приходим только
        // раздавать — карта полная, проверять нечего.
        m_finalPath = m_s.finalPath;
        for (quint64 i = 0; i < n; ++i)
            m_have.set(i);
        m_received = n;
        m_finished = true;
        m_disk.open(m_finalPath, tree, &m_s.volume.layout);
        m_tick->start();
        setPhase(QStringLiteral("done"));
        if (m_s.connect && m_seeding)
            connectRelay();
        return;
    }

    m_resumed = restore();
    if (!m_resumed && QFileInfo::exists(m_s.partPath)) {
        // Недокачка наша (путь дал файл состояния), но верить ей нельзя:
        // половина чужого тома под этим именем хуже, чем ничего.
        if (tree)
            QDir(m_s.partPath).removeRecursively();
        else
            QFile::remove(m_s.partPath);
    }
    m_received = m_have.cardinality();

    // Места хватит? Сеть быстрее диска не станет, а упереться в полный диск
    // на середине тома — худший из способов об этом узнать.
    const quint64 left = m_s.volume.plan.totalBytes
                         - std::min<quint64>(m_s.volume.plan.totalBytes,
                                             (m_have.cardinality() + m_pending.cardinality())
                                                 * quint64(m_s.volume.plan.chunkSize));
    QStorageInfo disk(QFileInfo(m_s.finalDir).absoluteFilePath());
    if (disk.isValid() && quint64(disk.bytesAvailable()) < left && !QFileInfo::exists(m_s.partPath)) {
        fail(QStringLiteral("no_space"),
             QStringLiteral("Нужно %1 МБ, свободно %2 МБ.")
                 .arg(left / (1024 * 1024)).arg(disk.bytesAvailable() / (1024 * 1024)));
        return;
    }

    m_disk.open(m_s.partPath, tree, &m_s.volume.layout);
    if (!m_disk.prepare(m_s.volume.manifest, &err)) {
        fail(QStringLiteral("io"), err);
        return;
    }
    m_dirty = true;
    saveState();
    m_tick->start();

    // Том уже целиком здесь и сверен (перезапуск после приёма, до
    // переименования) — к релею не идём: новое подключение без токена
    // возврата списало бы использование ссылки.
    if (m_have.full() && m_hashesComplete) {
        checkDone();
        return;
    }
    connectRelay();
}

// Недокачка из прошлого раза. Правила те же, что у веба и CLI:
//   * корень сошёлся с манифестом — карте верим целиком;
//   * корня нет (хеши ещё едут, или недокачка от такой раздачи) — каждый её
//     чанк перечитывается и сверяется с хешем, до того его как бы нет;
//   * корень другой — недокачка чужого тома, начинаем с нуля.
bool FerryReceiver::restore()
{
    QFile f(m_s.stateFile);
    if (!QFileInfo::exists(m_s.partPath) || !f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const ferry::ChunkPlan &p = m_s.volume.plan;
    if (o.value(QStringLiteral("v")).toInt() != 1
        || o.value(QStringLiteral("id")).toString() != m_s.id
        || quint64(o.value(QStringLiteral("count")).toDouble(-1)) != p.chunkCount
        || quint32(o.value(QStringLiteral("chunkSize")).toDouble()) != p.chunkSize
        || quint64(o.value(QStringLiteral("total")).toDouble(-1)) != p.totalBytes)
        return false;
    QByteArray bits;
    if (!fromB64url(o.value(QStringLiteral("bits")).toString(), bits)
        || size_t(bits.size()) != m_have.byteCount())
        return false;
    m_rejoin = o.value(QStringLiteral("rejoin")).toString();

    QByteArray root;
    const bool hasRoot = o.value(QStringLiteral("root")).isString()
                         && fromB64url(o.value(QStringLiteral("root")).toString(), root)
                         && root.size() == 32;
    const auto *raw = reinterpret_cast<const uint8_t *>(bits.constData());
    if (!m_s.meta.stream && hasRoot
        && std::memcmp(root.constData(), m_s.volume.manifest.root.data(), 32) == 0) {
        m_have.loadBits(raw, size_t(bits.size()));
        return m_have.cardinality() > 0;
    }
    if (m_s.meta.stream || !hasRoot) {
        m_pending.loadBits(raw, size_t(bits.size()));
        if (hasRoot) {
            std::memcpy(m_storedRoot.data(), root.constData(), 32);
            m_hasStoredRoot = true;
        }
        return m_pending.cardinality() > 0;
    }
    return false;
}

// ---------- соединение ----------

void FerryReceiver::connectRelay()
{
    if (m_stopped || m_failed)
        return;
    m_reconnect->stop();
    setPhase(QStringLiteral("connecting"));
    // Challenge одноразовый и живёт две минуты, поэтому берётся прямо перед
    // подключением.
    QNetworkReply *reply = m_nam->get(
        FerryApi::request(m_s.origin, QStringLiteral("/api/transfers/%1/challenge").arg(m_s.id)));
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onChallenge(reply); });
}

void FerryReceiver::onChallenge(QNetworkReply *reply)
{
    reply->deleteLater();
    if (m_stopped || m_failed)
        return;
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200) {
        const QString code = FerryApi::errorCode(reply, body);
        // Том уже у нас, просились только раздавать — не пустили, и ладно.
        if (m_finished) {
            seedRefusedWith(code);
            return;
        }
        if (code == QLatin1String("network") || code.startsWith(QLatin1String("http_5")))
            scheduleReconnect();
        else
            fail(code);
        return;
    }
    QByteArray challenge;
    if (!fromB64url(QJsonDocument::fromJson(body).object().value(QStringLiteral("challenge")).toString(),
                    challenge)
        || challenge.size() != int(ferry::kChallengeSize)) {
        fail(QStringLiteral("bad_message"), QStringLiteral("Релей прислал challenge не той длины."));
        return;
    }
    const ferry::Bytes proof = ferry::proveKeyOwnership(
        m_s.volume.keys.verifier, reinterpret_cast<const uint8_t *>(challenge.constData()),
        size_t(challenge.size()));
    if (proof.size() != 32) {
        fail(QStringLiteral("io"), QStringLiteral("Не посчиталось доказательство владения ключом."));
        return;
    }

    // Сегменты хешей релей присылает новому сокету с самого начала — значит
    // и считать их начинаем заново. Досчитанный список просить незачем.
    const bool wantStream = m_s.meta.stream && !m_hashesComplete;
    if (wantStream)
        m_hashed = 0;

    QString ws = m_s.origin;
    ws.replace(0, 4, QStringLiteral("ws"));   // http -> ws, https -> wss
    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_accepted = false;
    m_waitingApproval = false;
    m_ranges = false;
    m_sentHave = -1;
    m_lastAck = 0;
    m_outstanding = 0;
    m_sessionStart = m_have.prefix();
    m_socketOpenedAt = now();
    m_lastMessageAt = now();

    QWebSocket *sock = m_ws;
    const QByteArray proofRaw(reinterpret_cast<const char *>(proof.data()), int(proof.size()));
    connect(sock, &QWebSocket::connected, this, [this, sock, challenge, proofRaw, wantStream] {
        if (sock == m_ws)
            sendHello(challenge, proofRaw, wantStream);
    });
    connect(sock, &QWebSocket::textMessageReceived, this, [this, sock](const QString &t) {
        if (sock != m_ws)
            return;
        m_lastMessageAt = now();
        onText(t);
    });
    connect(sock, &QWebSocket::binaryMessageReceived, this, [this, sock](const QByteArray &d) {
        if (sock != m_ws)
            return;
        m_lastMessageAt = now();
        onBinary(d);
    });
    connect(sock, &QWebSocket::bytesWritten, this, [this, sock](qint64 n) {
        if (sock == m_ws)
            m_outstanding = qMax<qint64>(0, m_outstanding - n);
    });
    connect(sock, &QWebSocket::disconnected, this, [this, sock] { socketGone(sock); });
    sock->open(QUrl(ws + QLatin1String(ferry::kWsPath)));
}

void FerryReceiver::sendHello(const QByteArray &challenge, const QByteArray &proof, bool wantStream)
{
    QJsonObject hello{
        {QStringLiteral("type"), QStringLiteral("hello")},
        {QStringLiteral("id"), m_s.id},
        {QStringLiteral("challenge"), b64url(challenge)},
        {QStringLiteral("proof"), b64url(proof)},
        {QStringLiteral("have_upto"), double(m_have.prefix())},
        {QStringLiteral("have"), rangesJson(m_have.ranges())},
    };
    // backfill — «умею отдавать»: читать принятое мы можем всегда, но
    // объявлять это без согласия раздавать — значит позвать релей просить у
    // нас то, чего мы не отдадим, и повесить чужой догон.
    QJsonArray features{QStringLiteral("ranges"), QStringLiteral("stream_hashes")};
    if (m_seeding)
        features.append(QStringLiteral("backfill"));
    hello.insert(QStringLiteral("features"), features);
    if (!m_s.name.isEmpty())
        hello.insert(QStringLiteral("name"), m_s.name.left(64));
    if (!m_rejoin.isEmpty())
        hello.insert(QStringLiteral("rejoin"), m_rejoin);
    if (wantStream)
        hello.insert(QStringLiteral("hashes"), QStringLiteral("stream"));
    m_ws->sendTextMessage(QString::fromUtf8(QJsonDocument(hello).toJson(QJsonDocument::Compact)));
}

void FerryReceiver::sendJson(const QVariantMap &obj)
{
    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState)
        m_ws->sendTextMessage(
            QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(obj)).toJson(QJsonDocument::Compact)));
}

// Сокет умер — своей смертью или мы его бросили. Решаем, что дальше.
void FerryReceiver::socketGone(QWebSocket *ws)
{
    ws->deleteLater();
    if (ws != m_ws)
        return;
    m_ws = nullptr;
    if (m_failed || m_stopped)
        return;
    if (m_finished) {
        emitProgress();   // раздавать больше некуда — карточка это покажет
        return;
    }
    scheduleReconnect();
}

// Бросить сокет, не дожидаясь закрытия: пропавшая сеть может «закрываться»
// минутами — TCP ещё ждёт, а приём стоит.
void FerryReceiver::abandonSocket()
{
    QWebSocket *ws = m_ws;
    if (!ws)
        return;
    ws->disconnect(this);
    socketGone(ws);
    ws->abort();
}

void FerryReceiver::closeSocket()
{
    QWebSocket *ws = m_ws;
    if (!ws)
        return;
    m_ws = nullptr;
    ws->disconnect(this);
    if (ws->state() == QAbstractSocket::ConnectedState)
        ws->sendTextMessage(QStringLiteral("{\"type\":\"bye\"}"));
    // Close-фрейму надо дать уйти, иначе релей увидит не «ушёл», а «пропал»
    // и будет ждать нас минуту. Отпускаем по disconnected, со страховкой.
    connect(ws, &QWebSocket::disconnected, ws, &QObject::deleteLater);
    QTimer::singleShot(3000, ws, &QObject::deleteLater);
    ws->close();
}

void FerryReceiver::scheduleReconnect()
{
    if (m_stopped || m_failed || m_finished)
        return;
    const int steps = int(sizeof(kReconnectSteps) / sizeof(kReconnectSteps[0]));
    const int step = kReconnectSteps[std::min(m_reconnectAttempt, steps - 1)];
    ++m_reconnectAttempt;
    m_reconnectAt = now() + step * 1000;
    setPhase(QStringLiteral("reconnecting"));
    m_reconnect->start(step * 1000);
}

void FerryReceiver::reconnectNow()
{
    if (m_phase == QLatin1String("reconnecting"))
        connectRelay();
}

// ---------- сообщения ----------

void FerryReceiver::onText(const QString &text)
{
    const QVariantMap v = QJsonDocument::fromJson(text.toUtf8()).object().toVariantMap();
    const QString type = v.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("approval_pending")) {
        // Ключ принят, решает отправитель. Сколько он будет думать —
        // неизвестно, поэтому сторож hello здесь молчит; уйти можно кнопкой,
        // и использование при этом не списано.
        m_waitingApproval = true;
        setPhase(QStringLiteral("approval"));
    } else if (type == QLatin1String("state")) {
        // Отправитель ушёл, разрешив раздаче жить без него, — или пропала
        // его сеть и релей ждёт его назад. Приёму это ничего не меняет,
        // человеку стоит знать.
        if (v.contains(QStringLiteral("state")))
            m_relayState = v.value(QStringLiteral("state")).toString();
        if (v.value(QStringLiteral("uses_left")).isValid())
            m_usesLeft = v.value(QStringLiteral("uses_left")).isNull()
                             ? -1 : v.value(QStringLiteral("uses_left")).toLongLong();
        m_senderAway = v.value(QStringLiteral("sender_away")).toBool();
        m_senderWaitUntil = m_senderAway
            ? now() + v.value(QStringLiteral("sender_wait_ms"), 30000).toLongLong() : 0;
        emitProgress();
    } else if (type == QLatin1String("hello_ok")) {
        m_accepted = true;
        m_waitingApproval = false;
        // Токен одноразовый: релей выдаёт новый на каждый вход.
        const QString rejoin = v.value(QStringLiteral("rejoin")).toString();
        if (!rejoin.isEmpty()) {
            m_rejoin = rejoin;
            m_dirty = true;
        }
        if (v.contains(QStringLiteral("state")))
            m_relayState = v.value(QStringLiteral("state")).toString();
        m_senderAway = v.value(QStringLiteral("sender_away")).toBool();
        m_reconnectAttempt = 0;
        m_ranges = v.value(QStringLiteral("features")).toStringList().contains(QStringLiteral("ranges"));
        if (v.value(QStringLiteral("uses_left")).isValid())
            m_usesLeft = v.value(QStringLiteral("uses_left")).isNull()
                             ? -1 : v.value(QStringLiteral("uses_left")).toLongLong();
        setPhase(m_finished ? QStringLiteral("done") : QStringLiteral("receiving"));
        // Что нам нужно: релей по этому выбирает, что тянуть второй волной.
        if (m_ranges && !m_finished) {
            const auto want = m_have.missing();
            if (!want.empty()) {
                QJsonObject req{{QStringLiteral("type"), QStringLiteral("request")},
                                {QStringLiteral("ranges"), rangesJson(want)},
                                {QStringLiteral("budget_bytes"),
                                 double(quint64(m_s.volume.plan.chunkSize) * 8)}};
                m_ws->sendTextMessage(QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact)));
            }
        }
        checkDone();
    } else if (type == QLatin1String("error")) {
        const QString reason = v.value(QStringLiteral("reason"), QStringLiteral("unknown")).toString();
        // Отправитель ушёл, а у нас всё на месте — не беда: том уже здесь.
        if (m_finished)
            seedRefusedWith(reason);
        else
            fail(reason);
    } else if (type == QLatin1String("stats")) {
        const QVariantMap s = v.value(QStringLiteral("src")).toMap();
        const double w = s.value(QStringLiteral("window")).toDouble();
        const double p = s.value(QStringLiteral("peers")).toDouble();
        const double snd = s.value(QStringLiteral("sender")).toDouble();
        if (w + p + snd > 0) {
            m_srcWindow = w;
            m_srcPeers = p;
            m_srcSender = snd;
        }
    } else if (type == QLatin1String("hashes")) {
        onHashSegment(v);
    } else if (type == QLatin1String("hashes_done")) {
        onHashesDone(v);
    } else if (type == QLatin1String("serve")) {
        const QVariantList ranges = v.value(QStringLiteral("ranges")).toList();
        for (const QVariant &r : ranges) {
            const QVariantList pair = r.toList();
            if (pair.size() < 2)
                continue;
            const qint64 from = pair.at(0).toLongLong();
            const qint64 to = pair.at(1).toLongLong();
            if (from < 0 || to < from || quint64(from) >= count())
                continue;
            m_serveQueue.setRange({quint64(from), std::min<quint64>(quint64(to), count() - 1)});
        }
        serve();
    }
}

// Сегмент списка: встык к уже известным, под K_meta и с меткой от своего
// начала — переставить или подсунуть чужой сегмент нельзя.
void FerryReceiver::onHashSegment(const QVariantMap &v)
{
    const auto bad = [this] {
        fail(QStringLiteral("bad_hashes"), QStringLiteral("Сегмент списка хешей не сошёлся."));
    };
    const qint64 from = v.value(QStringLiteral("from"), -1).toLongLong();
    const qint64 n = v.value(QStringLiteral("count"), -1).toLongLong();
    if (!m_s.meta.stream || m_hashesComplete || from < 0 || quint64(from) != m_hashed || n < 1
        || quint64(n) > kHashSegmentMax || quint64(from + n) > count()) {
        bad();
        return;
    }
    QByteArray sealed;
    if (!fromB64url(v.value(QStringLiteral("data")).toString(), sealed)) {
        bad();
        return;
    }
    ferry::Bytes opened;
    if (!ferry::openChunk(m_s.volume.keys.meta, prefix(), ferry::hashSegmentLabel(quint64(from)),
                          count(), reinterpret_cast<const uint8_t *>(sealed.constData()),
                          size_t(sealed.size()), opened)
        || opened.size() != size_t(n) * 32) {
        bad();
        return;
    }
    std::memcpy(m_hashes.data() + size_t(from) * 32, opened.data(), opened.size());
    m_hashed = quint64(from + n);
}

// Отправитель досчитал. Итоговый манифест — тот же том, но с корнем, и
// корень обязан сойтись с тем, что собрано из сегментов.
void FerryReceiver::onHashesDone(const QVariantMap &v)
{
    const auto bad = [this] {
        fail(QStringLiteral("bad_hashes"), QStringLiteral("Корень не сошёлся со списком хешей."));
    };
    if (!m_s.meta.stream) {
        bad();
        return;
    }
    if (m_hashesComplete)
        return;   // повтор после переподключения
    if (m_hashed != count()) {
        bad();
        return;
    }
    QByteArray sealed;
    ferry::Bytes opened;
    ferry::Manifest final;
    if (!fromB64url(v.value(QStringLiteral("manifest")).toString(), sealed)
        || !ferry::openChunk(m_s.volume.keys.meta, prefix(), ferry::kMetaLabelManifest, count(),
                             reinterpret_cast<const uint8_t *>(sealed.constData()),
                             size_t(sealed.size()), opened)
        || !ferry::Manifest::fromJson(std::string(opened.begin(), opened.end()), final, nullptr)
        || final.streamHashes
        || final.root != ferry::blake3(m_hashes.data(), m_hashes.size())
        || !ferrySameVolume(final, m_s.volume.manifest)) {
        bad();
        return;
    }
    m_s.volume.manifest.root = final.root;
    m_s.volume.manifest.streamHashes = false;
    m_hashesComplete = true;
    m_dirty = true;

    // Недокачка этого же тома, досчитанного когда-то раньше: корни совпали —
    // карте можно верить целиком, перечитывать незачем.
    if (!m_pending.empty() && m_hasStoredRoot && m_storedRoot == final.root) {
        for (quint64 i = m_pending.firstPresent(0); i < count(); i = m_pending.firstPresent(i + 1)) {
            if (m_have.set(i))
                ++m_received;
        }
        m_pending.reset(count());
    }
    checkDone();
}

void FerryReceiver::onBinary(const QByteArray &frame)
{
    if (frame.size() < kHeader) {
        fail(QStringLiteral("bad_message"), QStringLiteral("Релей прислал обрезанный фрейм."));
        return;
    }
    const auto *raw = reinterpret_cast<const uint8_t *>(frame.constData());
    if (raw[0] != ferry::OpChunk)
        return;
    quint64 index = 0;
    for (int i = 1; i <= 8; ++i)
        index = (index << 8) | raw[i];
    if (index >= count()) {
        fail(QStringLiteral("bad_message"), QStringLiteral("Релей прислал чанк с несуществующим номером."));
        return;
    }
    if (m_have.has(index))
        return;   // повтор — не беда

    ferry::Bytes plain;
    if (!ferry::openChunk(m_s.volume.keys.data, prefix(), index, count(), raw + kHeader,
                          size_t(frame.size() - kHeader), plain)) {
        reject(index, "aead");
        return;
    }
    if (plain.size() != m_s.volume.plan.sizeOf(index)) {
        reject(index, "length");
        return;
    }
    // Проверка против списка хешей — за O(1) и независимо от порядка. Именно
    // она делает безопасным приём чанков от кого угодно.
    if (index >= m_hashed || !hashMatches(index, plain.data(), plain.size())) {
        reject(index, "hash");
        return;
    }
    m_badInARow = 0;

    QString err;
    if (!m_disk.writeAt(m_s.volume.plan.offsetOf(index), plain.data(), plain.size(), &err)) {
        fail(QStringLiteral("io"), err);
        return;
    }
    m_have.set(index);
    m_pending.clear(index);
    ++m_received;
    m_dirty = true;

    // Какой волной приехал чанк. Релей этого не говорит и не должен, но
    // волна видна по порядку: всё, что пришло ПОЗАДИ уже виденного, — вторая.
    if (m_joinIndex == -2)
        m_joinIndex = index > m_sessionStart ? qint64(index) : -1;
    if (index >= m_liveFront)
        m_liveFront = index;
    else
        m_late.set(index);
    m_meter.add(qint64(plain.size()), now());
    checkDone();
}

bool FerryReceiver::hashMatches(quint64 index, const uint8_t *data, size_t len) const
{
    const ferry::Hash32 got = ferry::blake3(data, len);
    return std::memcmp(got.data(), m_hashes.data() + size_t(index) * 32, 32) == 0;
}

// Несошедшийся чанк не валит передачу: его выбрасывают, релей узнаёт, что
// источник солгал, и просит тот же индекс у другого.
void FerryReceiver::reject(quint64 index, const char *why)
{
    ++m_badInARow;
    if (m_ranges)
        sendJson({{QStringLiteral("type"), QStringLiteral("bad_chunk")},
                  {QStringLiteral("index"), double(index)},
                  {QStringLiteral("reason"), QString::fromLatin1(why)}});
    if (m_badInARow >= kMaxBadChunks)
        fail(QStringLiteral("chunk_mismatch"));
}

// ---------- фон ----------

void FerryReceiver::housekeeping()
{
    if (m_failed)
        return;
    verifyPending();
    if (m_failed)
        return;
    const qint64 t = now();
    // Сторож. Пока идёт приём, релей не молчит подолгу: чанки, stats,
    // повторный need через пять секунд тишины. Тишина в kStallMs — мёртвая
    // сеть, о которой сокет ещё не знает.
    if (m_ws && !m_finished
        && ((!m_accepted && !m_waitingApproval && t - m_socketOpenedAt > kHelloTimeoutMs)
            || (m_accepted && t - m_lastMessageAt > kStallMs)))
        abandonSocket();
    if (m_accepted && m_ws && !m_finished && t - m_lastAck > 300)
        reportHave();
    if (m_dirty && t - m_lastSave > 1000)
        saveState();
    if (t - m_lastEmit > 250)
        emitProgress();
    serve();
}

// Сверка недокачки, которой нельзя было верить на слово. Не дольше 30 мс за
// оборот: сеть не должна стоять, пока мы перечитываем диск.
void FerryReceiver::verifyPending()
{
    if (m_pending.empty())
        return;
    const qint64 until = now() + 30;
    std::vector<uint8_t> buf;
    while (now() < until) {
        const quint64 i = m_pending.firstPresent(0);
        if (i >= count() || i >= m_hashed)
            break;
        m_pending.clear(i);
        const quint32 len = m_s.volume.plan.sizeOf(i);
        buf.resize(len);
        if (m_disk.readAt(m_s.volume.plan.offsetOf(i), buf.data(), len)
            && hashMatches(i, buf.data(), len) && m_have.set(i)) {
            ++m_received;
            m_dirty = true;
        }
    }
    checkDone();
}

void FerryReceiver::reportHave()
{
    if (!m_ranges) {
        const quint64 prefix = m_have.prefix();
        if (qint64(prefix) != m_sentHave) {
            sendJson({{QStringLiteral("type"), QStringLiteral("ack")},
                      {QStringLiteral("upto"), double(prefix)}});
            m_sentHave = qint64(prefix);
            m_lastAck = now();
        }
        return;
    }
    if (qint64(m_have.cardinality()) != m_sentHave && m_ws
        && m_ws->state() == QAbstractSocket::ConnectedState) {
        const QJsonObject o{{QStringLiteral("type"), QStringLiteral("have")},
                            {QStringLiteral("ranges"), rangesJson(m_have.ranges())}};
        m_ws->sendTextMessage(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        m_sentHave = qint64(m_have.cardinality());
        m_lastAck = now();
    }
}

void FerryReceiver::saveState()
{
    m_dirty = false;
    m_lastSave = now();
    // Несверенные чанки пишутся в карту вместе с настоящими: обрыв до сверки
    // иначе выбросил бы их, и в следующий раз их качали бы заново.
    QByteArray bits(reinterpret_cast<const char *>(m_have.bits().data()), int(m_have.byteCount()));
    const std::vector<uint8_t> &pending = m_pending.bits();
    for (int i = 0; i < bits.size() && size_t(i) < pending.size(); ++i)
        bits[i] = char(uint8_t(bits[i]) | pending[size_t(i)]);
    QJsonObject o{
        {QStringLiteral("v"), 1},
        {QStringLiteral("id"), m_s.id},
        {QStringLiteral("origin"), m_s.origin},
        {QStringLiteral("name"), QString::fromStdString(m_s.volume.manifest.name)},
        {QStringLiteral("kind"), QString::fromStdString(m_s.volume.manifest.kind)},
        {QStringLiteral("total"), double(m_s.volume.plan.totalBytes)},
        {QStringLiteral("chunkSize"), double(m_s.volume.plan.chunkSize)},
        {QStringLiteral("count"), double(count())},
        {QStringLiteral("part"), m_s.partPath},
        {QStringLiteral("dir"), m_s.finalDir},
        {QStringLiteral("bits"), b64url(bits)},
        {QStringLiteral("rejoin"), m_rejoin},
        {QStringLiteral("complete"), m_finished},
        {QStringLiteral("final"), m_finalPath},
        {QStringLiteral("updatedAt"), double(QDateTime::currentMSecsSinceEpoch())},
    };
    const ferry::Hash32 &root = m_s.volume.manifest.root;
    o.insert(QStringLiteral("root"),
             m_hashesComplete ? QJsonValue(b64url(QByteArray(reinterpret_cast<const char *>(root.data()), 32)))
                              : QJsonValue());
    QSaveFile f(m_s.stateFile);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

// Отдаём то, что у нас попросили, — по чанку и с оглядкой на свою же
// очередь: чужой догон не должен мешать собственному приёму.
void FerryReceiver::serve()
{
    if (m_serving || !m_seeding)
        return;
    m_serving = true;
    std::vector<uint8_t> buf;
    while (!m_serveQueue.empty() && m_ws && m_ws->state() == QAbstractSocket::ConnectedState
           && m_outstanding < kSeedHighWater) {
        const quint64 i = m_serveQueue.firstPresent(0);
        if (i >= count())
            break;
        m_serveQueue.clear(i);
        if (!m_have.has(i))
            continue;   // релей найдёт другой источник
        const quint32 len = m_s.volume.plan.sizeOf(i);
        buf.resize(len);
        if (!m_disk.readAt(m_s.volume.plan.offsetOf(i), buf.data(), len)) {
            // Файл переименовали или удалили. Свою загрузку из-за чужой не
            // роняем: перестаём отдавать, и только.
            m_serveQueue.reset(count());
            break;
        }
        // Шифруем заново: AES-GCM детерминирован, и шифротекст выходит байт
        // в байт тем, что прислал отправитель. Сид не хранит шифротекст.
        ferry::Bytes sealed;
        if (!ferry::sealChunk(m_s.volume.keys.data, prefix(), i, count(), buf.data(), len, sealed))
            break;
        QByteArray frame(kHeader + int(sealed.size()), Qt::Uninitialized);
        frame[0] = char(ferry::OpChunk);
        for (int k = 0; k < 8; ++k)
            frame[1 + k] = char(uint8_t(i >> (56 - 8 * k)));
        std::memcpy(frame.data() + kHeader, sealed.data(), sealed.size());
        m_outstanding += frame.size() + 14;   // и заголовок кадра вебсокета
        m_ws->sendBinaryMessage(frame);
        ++m_served;
        m_servedBytes += len;
    }
    m_serving = false;
}

// ---------- конец ----------

void FerryReceiver::checkDone()
{
    if (m_finished || m_failed || m_received < count() || !m_hashesComplete || !m_pending.empty())
        return;
    // Последний have — обязательно: небольшой том уезжает быстрее, чем идут
    // отчёты, и релей иначе не узнал бы, что у нас всё есть, — а раздавать
    // нас после этого не позвали бы никогда.
    m_sentHave = -1;
    reportHave();
    QString err;
    if (!m_disk.flush(&err)) {
        fail(QStringLiteral("io"), err);
        return;
    }
    if (!finalizeVolume())
        return;
    m_finished = true;
    saveState();
    setPhase(QStringLiteral("done"));
    if (!m_seeding)
        closeSocket();
    emitProgress();
    emit done(m_finalPath);
}

// Недокачка становится томом: «<имя>.ferry-part» -> «<имя>» (или
// «<имя> (2)», если такое уже есть). Всё сразу и в самом конце — иначе на
// диске какое-то время лежал бы полутом под настоящим именем.
bool FerryReceiver::finalizeVolume()
{
    m_disk.close();
    const bool tree = m_s.volume.manifest.isTree();
    const QString name = QString::fromStdString(ferry::safeFileName(m_s.volume.manifest.name));
    const QString target = uniquePath(m_s.finalDir, name, tree);
    // Свежие файлы любит открывать антивирус и индексатор — переименование
    // каталога с открытым внутри файлом Windows отклоняет. Даём им секунду.
    bool ok = false;
    for (int attempt = 0; attempt < 10 && !ok; ++attempt) {
        ok = tree ? QDir().rename(m_s.partPath, target) : QFile::rename(m_s.partPath, target);
        if (!ok)
            QThread::msleep(150);
    }
    if (!ok) {
        fail(QStringLiteral("io"),
             QStringLiteral("Том принят, но не переименовался: он лежит в %1").arg(m_s.partPath));
        return false;
    }
    m_finalPath = target;
    m_disk.setRoot(target);
    return true;
}

void FerryReceiver::setSeeding(bool on)
{
    m_seeding = on;
    if (on)
        m_seedRefused.clear();
    if (!on && m_finished)
        closeSocket();
    // Том собран без релея (перезапуск) — раздавать значит подключиться.
    if (on && m_finished && !m_ws && !m_stopped && !m_failed)
        connectRelay();
    emitProgress();
}

void FerryReceiver::seedRefusedWith(const QString &code)
{
    m_seedRefused = code;
    m_seeding = false;
    closeSocket();
    setPhase(QStringLiteral("done"));
    emitProgress();
}

void FerryReceiver::stop()
{
    m_stopped = true;
    if (m_reconnect)
        m_reconnect->stop();
    closeSocket();
    if (m_tick)
        m_tick->stop();
    if (m_have.count() > 0 || m_finished)
        saveState();
    m_disk.close();
    setPhase(m_finished ? QStringLiteral("done") : QStringLiteral("stopped"));
    emit stopped();
}

void FerryReceiver::fail(const QString &code, const QString &message)
{
    if (m_failed)
        return;
    m_failed = true;
    m_failCode = code;
    m_failText = message;
    if (m_reconnect)
        m_reconnect->stop();
    closeSocket();
    if (m_have.count() > 0)
        saveState();
    m_disk.close();
    setPhase(QStringLiteral("failed"));
    emit failed(code, message, !FerryApi::isFinal(code));
}

void FerryReceiver::setPhase(const QString &phase)
{
    if (m_phase == phase)
        return;
    m_phase = phase;
    emitProgress();
}

void FerryReceiver::emitProgress()
{
    m_lastEmit = now();
    emit progress(snapshot());
}

QVariantMap FerryReceiver::snapshot() const
{
    const ferry::ChunkPlan &p = m_s.volume.plan;
    const quint64 n = count();
    const bool lastHave = n > 0 && m_have.has(n - 1);
    const quint64 bytes = n == 0 ? 0
        : m_have.cardinality() * quint64(p.chunkSize)
              - (lastHave ? quint64(p.chunkSize) - p.sizeOf(n - 1) : 0);

    // Карта тома: kMapCells клеток, в каждой — сколько принято и какой
    // волной. 0 — нет, 1 — частично, 2 — есть, 3 — есть, вторая волна.
    QVariantList cells;
    const quint64 cellsN = n == 0 ? 1 : std::min<quint64>(kMapCells, n);
    for (quint64 c = 0; c < cellsN; ++c) {
        if (n == 0) {
            cells.append(m_finished ? 2 : 0);
            continue;
        }
        const quint64 a = c * n / cellsN;
        const quint64 b = std::max(a + 1, (c + 1) * n / cellsN);
        quint64 got = 0, late = 0;
        for (quint64 i = a; i < b; ++i) {
            if (m_have.has(i)) {
                ++got;
                if (m_late.has(i))
                    ++late;
            }
        }
        cells.append(got == 0 ? 0 : got < b - a ? 1 : late * 2 > got ? 3 : 2);
    }

    const qint64 t = now();
    return {
        {QStringLiteral("phase"), m_phase},
        {QStringLiteral("received"), double(m_received)},
        {QStringLiteral("count"), double(n)},
        {QStringLiteral("bytes"), double(bytes)},
        {QStringLiteral("total"), double(p.totalBytes)},
        {QStringLiteral("rate"), m_meter.current(t)},
        {QStringLiteral("pending"), double(m_pending.cardinality())},
        {QStringLiteral("hashesComplete"), m_hashesComplete},
        {QStringLiteral("cells"), cells},
        {QStringLiteral("reconnectInMs"),
         m_phase == QLatin1String("reconnecting") ? double(qMax<qint64>(0, m_reconnectAt - t)) : 0.0},
        {QStringLiteral("relayState"), m_relayState},
        {QStringLiteral("senderAway"), m_senderAway},
        {QStringLiteral("senderWaitMs"), double(qMax<qint64>(0, m_senderWaitUntil - t))},
        {QStringLiteral("served"), double(m_served)},
        {QStringLiteral("servedBytes"), double(m_servedBytes)},
        {QStringLiteral("seeding"), m_seeding && m_finished && m_ws != nullptr},
        {QStringLiteral("seedRefused"), m_seedRefused},
        {QStringLiteral("usesLeft"), double(m_usesLeft)},
        {QStringLiteral("resumed"), m_resumed},
        {QStringLiteral("srcPeers"), m_srcPeers},
        {QStringLiteral("srcSender"), m_srcSender},
        {QStringLiteral("srcWindow"), m_srcWindow},
        {QStringLiteral("finalPath"), m_finalPath},
        {QStringLiteral("failCode"), m_failCode},
        {QStringLiteral("failText"), m_failText},
    };
}
