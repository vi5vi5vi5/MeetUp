#include "FerrySender.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QWebSocket>

#include <cstring>

#include "FerryVolume.h"
#include "core/HashList.h"
#include "core/Link.h"
#include "core/Protocol.h"

namespace {

// Сколько держим в очереди сокета. Меньше — на быстром канале между чанками
// паузы; больше — шифруем то, что уедет через полминуты.
constexpr qint64 kHighWater = 16 * 1024 * 1024;
// Доля догона в аплоаде — три десятых, как у CLI и веба (§6).
constexpr quint64 kLiveShare = 7;
constexpr quint64 kBackfillShare = 3;
constexpr quint64 kSegmentMax = 8192;
constexpr int kOfferTimeoutMs = 20000;
constexpr qint64 kProgressMs = 250;
// Релей ждёт отправителя тридцать секунд; пробуем двадцать пять — чтобы не
// прийти к уже закрытой раздаче.
constexpr qint64 kResumeWindowMs = 25000;
// Релей шлёт сводку раз в секунду; тишина дольше — соединение мертво, даже
// если сокет об этом ещё не знает.
constexpr qint64 kSilenceMs = 15000;

QString b64url(const uint8_t *p, size_t n)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(p), int(n))
                                   .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
QString b64url(const QByteArray &b)
{
    return QString::fromLatin1(b.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

} // namespace

// ---------- хешер ----------

FerryHasher::FerryHasher(const ferry::Manifest &manifest, std::vector<QString> sources,
                         ferry::ChunkPlan plan, QObject *parent)
    : QThread(parent), m_manifest(manifest), m_sources(std::move(sources)), m_plan(plan)
{
    m_layout.build(m_manifest);
}

void FerryHasher::run()
{
    FerryDisk disk;
    disk.open(QString(), true, &m_layout);
    disk.setPaths(m_sources);
    std::vector<uint8_t> buf;
    QByteArray batch;
    quint64 batchFrom = 0;
    quint64 bytes = 0;
    QElapsedTimer since;
    since.start();
    for (quint64 i = 0; i < m_plan.chunkCount; ++i) {
        if (m_stop)
            return;
        const quint32 len = m_plan.sizeOf(i);
        buf.resize(len);
        if (!disk.readAt(m_plan.offsetOf(i), buf.data(), len)) {
            emit failed(QStringLiteral("Не прочитался кусок тома — файл изменили, переместили или удалили?"));
            return;
        }
        const ferry::Hash32 h = ferry::blake3(buf.data(), len);
        batch.append(reinterpret_cast<const char *>(h.data()), 32);
        bytes += len;
        // Пачками: по 256 хешей или раз в 200 мс — чтобы ссылка на огромный
        // том не ждала, а маленький не засыпал отправителя сигналами.
        if (batch.size() >= 256 * 32 || since.elapsed() > 200) {
            emit hashed(batchFrom, batch, bytes);
            batchFrom = i + 1;
            batch.clear();
            since.restart();
        }
    }
    if (!batch.isEmpty())
        emit hashed(batchFrom, batch, bytes);
    emit finishedAll();
}

// ---------- отправитель ----------

void FerrySender::Meter::add(qint64 n, qint64 t)
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

double FerrySender::Meter::read(qint64 t) const
{
    return last && t - last > 2000 ? 0.0 : value;
}

FerrySender::FerrySender(Setup setup, QObject *parent) : QObject(parent), m_s(std::move(setup)) {}

FerrySender::~FerrySender()
{
    if (m_hasher) {
        m_hasher->cancel();
        m_hasher->wait();
    }
    if (m_ws) {
        m_ws->disconnect(this);
        m_ws->abort();
    }
    m_master.fill(0);
}

void FerrySender::start()
{
    m_clock.start();
    m_nam = new QNetworkAccessManager(this);
    m_tick = new QTimer(this);
    m_tick->setInterval(50);
    connect(m_tick, &QTimer::timeout, this, &FerrySender::tick);

    m_plan = ferry::planFor(m_s.manifest.total);
    m_layout.build(m_s.manifest);
    m_reader.open(QString(), true, &m_layout);
    m_reader.setPaths(m_s.sources);
    const quint64 n = m_plan.chunkCount;
    m_hashes.assign(size_t(n) * 32, 0);
    m_live.reset(n);
    m_backfill.reset(n);
    m_sent.reset(n);

    bool ok = true;
    if (m_s.password) {
        m_master = m_s.passwordKey;
        m_s.passwordKey.fill(0);
    } else {
        m_master = ferry::randomKey32(&ok);
    }
    if (!ok || !ferry::randomBytes(m_noncePrefix, sizeof(m_noncePrefix))) {
        fail(QStringLiteral("internal"), QStringLiteral("Генератор случайных чисел системы не ответил."));
        return;
    }
    m_keys = ferry::TransferKeys::derive(m_master);
    m_keyText = m_s.password ? QString() : b64url(m_master.data(), m_master.size());
    m_wantApproval = m_s.policy.requireApproval;

    // Хеши — сразу и в своём потоке: с хешами на лету ссылка от них не
    // зависит, без — ждёт их, и каждая секунда здесь на счету.
    m_hasher = new FerryHasher(m_s.manifest, m_s.sources, m_plan, this);
    connect(m_hasher, &FerryHasher::hashed, this, &FerrySender::onHashed);
    connect(m_hasher, &FerryHasher::failed, this, [this](const QString &m) { fail(QStringLiteral("read"), m); });
    connect(m_hasher, &FerryHasher::finishedAll, this, &FerrySender::onHashingDone);
    m_hasher->start(QThread::LowPriority);

    createDraft();
}

// Черновик раздачи живёт минуту: столько есть, чтобы дойти до сокета с offer.
void FerrySender::createDraft()
{
    setPhase(QStringLiteral("creating"));
    m_draftAt = now();
    QNetworkRequest req = FerryApi::request(m_s.origin, QString::fromLatin1(ferry::kApiTransfers));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("text/plain"));
    QNetworkReply *reply = m_nam->post(req, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onCreated(reply); });
}

void FerrySender::onCreated(QNetworkReply *reply)
{
    reply->deleteLater();
    if (m_finished)
        return;
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 201 && status != 200) {
        fail(FerryApi::errorCode(reply, body));
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    m_id = o.value(QStringLiteral("id")).toString();
    m_ownerToken = o.value(QStringLiteral("owner_token")).toString();
    m_publicUrl = o.value(QStringLiteral("public_url")).toString();
    while (m_publicUrl.endsWith(QLatin1Char('/')))
        m_publicUrl.chop(1);
    m_features.clear();
    for (const QJsonValue &f : o.value(QStringLiteral("features")).toArray())
        m_features.append(f.toString());
    if (!ferry::isValidTransferId(m_id.toStdString()) || m_ownerToken.isEmpty()) {
        fail(QStringLiteral("not_ferry"), QStringLiteral("Релей ответил не тем."));
        return;
    }
    m_stream = m_features.contains(QLatin1String(ferry::kFeatureStreamHashes));
    m_canResume = m_features.contains(QLatin1String(ferry::kFeatureResume));
    // Чего релей не умеет — говорим до ссылки, а не после первого
    // получателя: старый релей молча пропустил бы условия мимо ушей.
    if (m_s.password && !m_features.contains(QLatin1String(ferry::kFeaturePassword))) {
        fail(QStringLiteral("unsupported"), QStringLiteral("Этот релей не умеет раздачу по паролю."));
        return;
    }
    if ((m_s.policy.requireApproval || m_s.policy.leaveOpen)
        && !m_features.contains(QLatin1String(ferry::kFeaturePolicy))) {
        fail(QStringLiteral("unsupported"),
             QStringLiteral("Этот релей не умеет подтверждение и раздачу без отправителя."));
        return;
    }
    // Релей без хешей на лету: весь том — до ссылки.
    if (!m_stream && !m_hashingDone) {
        m_waitingHashes = true;
        setPhase(QStringLiteral("hashing"));
        return;
    }
    beginOffer();
}

QByteArray FerrySender::sealMeta(uint64_t label, const uint8_t *plain, size_t len) const
{
    ferry::Bytes out;
    ferry::sealChunk(m_keys.meta, m_noncePrefix, label, m_plan.chunkCount, plain, len, out);
    return QByteArray(reinterpret_cast<const char *>(out.data()), int(out.size()));
}

void FerrySender::sendFinalMeta(QJsonObject *offer)
{
    ferry::Manifest final = m_s.manifest;
    final.streamHashes = false;
    final.root = ferry::blake3(m_hashes.data(), m_hashes.size());
    const std::string json = final.toJson();
    const QByteArray manifest = sealMeta(ferry::kMetaLabelManifest,
                                         reinterpret_cast<const uint8_t *>(json.data()), json.size());
    const QByteArray list = sealMeta(ferry::kMetaLabelHashList, m_hashes.data(), m_hashes.size());
    if (offer) {
        offer->insert(QStringLiteral("manifest"), b64url(manifest));
        offer->insert(QStringLiteral("hash_list"), b64url(list));
        return;
    }
    sendJson({{QStringLiteral("type"), QStringLiteral("hashes_done")},
              {QStringLiteral("manifest"), b64url(manifest)},
              {QStringLiteral("hash_list"), b64url(list)}});
}

void FerrySender::beginOffer()
{
    const QString base = m_publicUrl.isEmpty() ? m_s.origin : m_publicUrl;
    m_link = base + QStringLiteral("/t/") + m_id;
    m_ownerLink = base + QStringLiteral("/#manage=") + m_id + QLatin1Char('.') + m_ownerToken;
    if (!m_s.password) {
        m_link += QLatin1Char('#') + m_keyText;
        m_ownerLink += QLatin1Char('.') + m_keyText;
    }

    QJsonObject policy;
    if (m_s.policy.uses > 0)
        policy.insert(QStringLiteral("uses"), double(m_s.policy.uses));
    if (m_s.policy.ttlMs > 0)
        policy.insert(QStringLiteral("ttl_ms"), double(m_s.policy.ttlMs));
    if (m_s.policy.maxConcurrent > 0)
        policy.insert(QStringLiteral("max_concurrent"), m_s.policy.maxConcurrent);
    if (m_s.policy.requireApproval)
        policy.insert(QStringLiteral("require_approval"), true);
    // «Без меня» — значит и пускать без меня: после ухода спрашивать некого,
    // и подтверждение снимается (freeze).
    if (m_s.policy.leaveOpen) {
        policy.insert(QStringLiteral("leave_open"), true);
        policy.insert(QStringLiteral("on_owner_leave"), QStringLiteral("freeze"));
    }

    QJsonObject offer{
        {QStringLiteral("type"), QStringLiteral("offer")},
        {QStringLiteral("id"), m_id},
        {QStringLiteral("owner_token"), m_ownerToken},
        {QStringLiteral("total"), double(m_plan.totalBytes)},
        {QStringLiteral("chunk_size"), double(m_plan.chunkSize)},
        {QStringLiteral("chunks"), double(m_plan.chunkCount)},
        {QStringLiteral("nonce_prefix"), b64url(m_noncePrefix, sizeof(m_noncePrefix))},
        {QStringLiteral("verifier"), b64url(m_keys.verifier.data(), m_keys.verifier.size())},
        {QStringLiteral("mode"), m_s.password ? QStringLiteral("password") : QStringLiteral("key")},
        {QStringLiteral("policy"), policy},
    };
    if (m_s.password)
        offer.insert(QStringLiteral("salt"), b64url(m_s.salt));
    if (m_stream) {
        ferry::Manifest interim = m_s.manifest;
        interim.streamHashes = true;
        const std::string json = interim.toJson();
        offer.insert(QStringLiteral("hash_mode"), QStringLiteral("stream"));
        offer.insert(QStringLiteral("manifest"),
                     b64url(sealMeta(ferry::kMetaLabelStreamManifest,
                                     reinterpret_cast<const uint8_t *>(json.data()), json.size())));
    } else {
        sendFinalMeta(&offer);
        m_announced = m_plan.chunkCount;
        m_hashesDoneSent = true;
    }
    if (m_canResume)
        offer.insert(QStringLiteral("features"), QJsonArray{QString::fromLatin1(ferry::kFeatureResume)});
    m_offer = QJsonDocument(offer).toJson(QJsonDocument::Compact);
    openSocket(false);
}

void FerrySender::openSocket(bool resume)
{
    if (!resume)
        setPhase(QStringLiteral("offering"));
    QString url = m_s.origin;
    url.replace(0, 4, QStringLiteral("ws"));
    auto *ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_ws = ws;
    m_outstanding = 0;
    connect(ws, &QWebSocket::connected, this, [this, ws, resume] {
        if (ws != m_ws)
            return;
        if (resume) {
            sendJson({{QStringLiteral("type"), QStringLiteral("resume_owner")},
                      {QStringLiteral("id"), m_id},
                      {QStringLiteral("owner_token"), m_ownerToken},
                      {QStringLiteral("serve"), true},
                      {QStringLiteral("features"), QJsonArray{QString::fromLatin1(ferry::kFeatureResume)}}});
        } else {
            ws->sendTextMessage(QString::fromUtf8(m_offer));
        }
    });
    connect(ws, &QWebSocket::textMessageReceived, this, [this, ws](const QString &t) {
        if (ws == m_ws)
            onText(t);
    });
    connect(ws, &QWebSocket::bytesWritten, this, [this, ws](qint64 n) {
        if (ws != m_ws)
            return;
        m_outstanding = qMax<qint64>(0, m_outstanding - n);
        kick();
    });
    connect(ws, &QWebSocket::disconnected, this, [this, ws] { socketClosed(ws); });
    if (!resume) {
        QTimer::singleShot(kOfferTimeoutMs, this, [this, ws] {
            if (!m_accepted && !m_finished && ws == m_ws)
                fail(QStringLiteral("network"), QStringLiteral("Релей не подтвердил раздачу."));
        });
    } else {
        // Попытка вернуться — не дольше десяти секунд, дальше следующая.
        QTimer::singleShot(10000, this, [this, ws] {
            if (m_reconnecting && ws == m_ws && !m_finished)
                ws->abort();
        });
    }
    ws->open(QUrl(url + QLatin1String(ferry::kWsPath)));
}

void FerrySender::sendJson(const QJsonObject &o)
{
    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState)
        m_ws->sendTextMessage(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

void FerrySender::onText(const QString &text)
{
    const QVariantMap v = QJsonDocument::fromJson(text.toUtf8()).object().toVariantMap();
    const QString type = v.value(QStringLiteral("type")).toString();
    m_lastHeard = now();

    if (m_reconnecting) {
        if (type == QLatin1String("resume_ok"))
            adopt(v);
        else if (type == QLatin1String("error"))
            fail(v.value(QStringLiteral("reason"), QStringLiteral("unknown")).toString());
        return;
    }
    if (type == QLatin1String("offer_ok")) {
        if (!m_accepted) {
            m_accepted = true;
            onAccepted();
        }
    } else if (type == QLatin1String("need")) {
        // need может прийти одной пачкой с offer_ok и раньше него — не теряем.
        rememberNeed(v);
        kick();
    } else if (type == QLatin1String("peers")) {
        m_receivers = v.value(QStringLiteral("receivers")).toList();
    } else if (type == QLatin1String("approval_request")) {
        emit knock(v.value(QStringLiteral("receiver")).toLongLong(), v.value(QStringLiteral("name")).toString());
    } else if (type == QLatin1String("approval_cancel")) {
        emit knockGone(v.value(QStringLiteral("receiver")).toLongLong());
    } else if (type == QLatin1String("state")) {
        m_state = v;
        emit relayState(v);
    } else if (type == QLatin1String("error")) {
        m_lastError = v.value(QStringLiteral("reason"), QStringLiteral("unknown")).toString();
        fail(m_lastError);
    }
}

void FerrySender::onAccepted()
{
    setPhase(QStringLiteral("live"));
    emit ready(m_link, m_ownerLink, m_id, m_ownerToken);
    m_lastHeard = now();
    m_tick->start();
    kick();
}

// Сокет закрылся. Релей объяснил почему — это конец; молча — обрыв, и
// релей, обещавший подождать, ждёт нас назад.
void FerrySender::socketClosed(QWebSocket *ws)
{
    ws->deleteLater();
    if (ws != m_ws || m_finished)
        return;
    m_ws = nullptr;
    if (!m_accepted) {
        fail(m_lastError.isEmpty() ? QStringLiteral("network") : m_lastError,
             QStringLiteral("Не удалось подключиться к релею."));
        return;
    }
    if (m_reconnecting) {
        // Попытка не удалась — следующая, пока не вышло окно ожидания релея.
        if (now() >= m_resumeDeadline) {
            fail(QStringLiteral("network"), QStringLiteral("Связь с релеем пропала и не вернулась."));
            return;
        }
        const int pause = int(std::min<qint64>(m_resumeDelay, m_resumeDeadline - now()));
        m_resumeDelay = std::min(m_resumeDelay * 2, 4000);
        QTimer::singleShot(pause, this, &FerrySender::tryResume);
        return;
    }
    if (!m_lastError.isEmpty() || !m_canResume) {
        fail(m_lastError.isEmpty() ? QStringLiteral("network") : m_lastError);
        return;
    }
    lost();
}

void FerrySender::lost()
{
    if (m_reconnecting || m_finished)
        return;
    m_reconnecting = true;
    if (QWebSocket *old = m_ws) {
        m_ws = nullptr;
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    m_resumeDeadline = now() + kResumeWindowMs;
    m_resumeDelay = 500;
    setPhase(QStringLiteral("lost"));
    emitProgress(true);
    tryResume();
}

void FerrySender::tryResume()
{
    if (m_finished || !m_reconnecting)
        return;
    if (now() >= m_resumeDeadline) {
        fail(QStringLiteral("network"), QStringLiteral("Связь с релеем пропала и не вернулась."));
        return;
    }
    openSocket(true);
}

// Вернулись. Что было в пути, пропало вместе с прежним сокетом: очереди
// начинаются заново, релей попросит недостающее сам. Хеши — с того места,
// докуда он их принял.
void FerrySender::adopt(const QVariantMap &v)
{
    m_reconnecting = false;
    m_lastHeard = now();
    m_live.reset(m_plan.chunkCount);
    m_backfill.reset(m_plan.chunkCount);
    m_liveHint = m_backfillHint = 0;
    m_outstanding = 0;
    if (m_stream) {
        const quint64 upto = quint64(qMax<qint64>(0, v.value(QStringLiteral("hashed_upto")).toLongLong()));
        m_announced = std::min(m_announced, upto);
        m_hashesDoneSent = v.value(QStringLiteral("hashes_complete")).toBool();
    }
    // Пока нас не было, раздача могла уйти на сиды, и подтверждение
    // снялось. Вернувшись, включаем его снова — так хотел человек.
    if (m_wantApproval && !v.value(QStringLiteral("require_approval")).toBool())
        sendJson({{QStringLiteral("type"), QStringLiteral("policy")},
                  {QStringLiteral("require_approval"), true}});
    m_state = v;
    emit relayState(v);
    setPhase(QStringLiteral("live"));
    kick();
}

void FerrySender::rememberNeed(const QVariantMap &v)
{
    const bool backfill = v.value(QStringLiteral("lane")).toString() == QLatin1String("backfill");
    ferry::ChunkSet &queue = backfill ? m_backfill : m_live;
    for (const QVariant &r : v.value(QStringLiteral("ranges")).toList()) {
        const QVariantList pair = r.toList();
        if (pair.size() < 2)
            continue;
        const qint64 from = pair.at(0).toLongLong();
        const qint64 to = pair.at(1).toLongLong();
        if (from < 0 || to < from || quint64(from) >= m_plan.chunkCount)
            continue;
        if (!queue.setRange({quint64(from), std::min<quint64>(quint64(to), m_plan.chunkCount - 1)}))
            continue;
        quint64 &hint = backfill ? m_backfillHint : m_liveHint;
        hint = std::min(hint, quint64(from));
    }
}

// ---------- хеши ----------

void FerrySender::onHashed(quint64 from, const QByteArray &hashes, quint64 bytes)
{
    if (m_finished)
        return;
    if (from != m_hashed || hashes.size() % 32 != 0
        || from * 32 + quint64(hashes.size()) > m_hashes.size()) {
        fail(QStringLiteral("internal"), QStringLiteral("Хеши пришли не по порядку."));
        return;
    }
    std::memcpy(m_hashes.data() + from * 32, hashes.constData(), size_t(hashes.size()));
    m_hashed = from + quint64(hashes.size()) / 32;
    m_hashMeter.add(qint64(bytes - m_hashBytes), now());
    m_hashBytes = bytes;
    if (m_accepted)
        kick();
    else
        emitProgress(false);
}

void FerrySender::onHashingDone()
{
    m_hashingDone = true;
    if (m_waitingHashes) {
        m_waitingHashes = false;
        // Черновик живёт минуту, а подсчёт мог идти дольше — тогда свежий.
        if (now() - m_draftAt > 45000)
            createDraft();
        else
            beginOffer();
        return;
    }
    kick();
}

// Сегменты — пачками: по 256 хешей или раз в 300 мс. Но если живая волна
// упёрлась в чанк, чей хеш ещё не объявлен, — сразу.
bool FerrySender::announce()
{
    if (!m_stream || m_hashesDoneSent || !m_ws || m_reconnecting)
        return false;
    bool sent = false;
    const quint64 count = m_plan.chunkCount;
    const quint64 ready = m_hashed;
    if (ready > m_announced) {
        const quint64 nextLive = m_live.empty() ? count : m_live.firstPresent(0);
        const bool starving = nextLive >= m_announced && nextLive < ready;
        const bool batch = ready - m_announced >= 256;
        const bool stale = now() - m_lastSegment >= 300;
        if (starving || batch || stale || ready == count) {
            while (m_announced < ready) {
                const quint64 from = m_announced;
                // После обрыва сегмент с того же места обязан быть ТОЧНО таким
                // же: nonce зависит только от начала.
                const auto known = m_segments.find(from);
                const quint64 n = known != m_segments.end() ? known->second
                                                            : std::min<quint64>(ready - from, kSegmentMax);
                if (from + n > ready)
                    break;
                m_segments[from] = n;
                const QByteArray sealed = sealMeta(ferry::hashSegmentLabel(from),
                                                   m_hashes.data() + from * 32, size_t(n) * 32);
                sendJson({{QStringLiteral("type"), QStringLiteral("hashes")},
                          {QStringLiteral("from"), double(from)},
                          {QStringLiteral("count"), double(n)},
                          {QStringLiteral("data"), b64url(sealed)}});
                m_announced = from + n;
                sent = true;
            }
            m_lastSegment = now();
        }
    }
    // Всё посчитано и объявлено — итоговые манифест с корнем и список: с
    // этой минуты раздача открыта и тем, кто хеши на лету не умеет.
    if (m_hashingDone && m_announced == count && !m_hashesDoneSent) {
        m_hashesDoneSent = true;
        sendFinalMeta(nullptr);
        sent = true;
    }
    return sent;
}

// ---------- чанки ----------

// Следующий чанк к отправке или -1. Живая волна первой, но догону — до трёх
// десятых: иначе при получателях быстрее нашего канала опоздавший не
// получил бы ничего.
qint64 FerrySender::pick()
{
    const quint64 count = m_plan.chunkCount;
    bool live = !m_live.empty();
    if (live && !m_backfill.empty() && m_sentBackfill * kLiveShare < m_sentLive * kBackfillShare)
        live = false;
    ferry::ChunkSet &queue = live ? m_live : m_backfill;
    if (queue.empty())
        return -1;
    quint64 &hint = live ? m_liveHint : m_backfillHint;
    quint64 index = queue.firstPresent(hint);
    if (index >= count)
        index = queue.firstPresent(0);
    if (index >= count)
        return -1;
    hint = index;
    // Чанк не уходит раньше своего хеша.
    if (index >= m_announced)
        return -1;
    queue.clear(index);
    (live ? m_sentLive : m_sentBackfill) += 1;
    return qint64(index);
}

void FerrySender::fill()
{
    std::vector<uint8_t> buf;
    while (!m_finished && m_ws && m_ws->state() == QAbstractSocket::ConnectedState
           && m_outstanding < kHighWater) {
        const qint64 index = pick();
        if (index < 0)
            return;
        const quint32 len = m_plan.sizeOf(quint64(index));
        buf.resize(len);
        if (!m_reader.readAt(m_plan.offsetOf(quint64(index)), buf.data(), len)) {
            fail(QStringLiteral("read"),
                 QStringLiteral("Не прочитался кусок тома — файл изменили, переместили или удалили?"));
            return;
        }
        ferry::Bytes sealed;
        if (!ferry::sealChunk(m_keys.data, m_noncePrefix, quint64(index), m_plan.chunkCount, buf.data(), len,
                              sealed)) {
            fail(QStringLiteral("internal"), QStringLiteral("Не зашифровался чанк."));
            return;
        }
        QByteArray frame(1 + 8 + int(sealed.size()), Qt::Uninitialized);
        frame[0] = char(ferry::OpChunk);
        for (int k = 0; k < 8; ++k)
            frame[1 + k] = char(uint8_t(quint64(index) >> (56 - 8 * k)));
        std::memcpy(frame.data() + 9, sealed.data(), sealed.size());
        m_outstanding += frame.size() + 14;
        m_ws->sendBinaryMessage(frame);
        m_meter.add(len, now());
        m_sentBytes += len;
        if (m_sent.set(quint64(index)))
            m_distinctBytes += len;
    }
}

void FerrySender::kick()
{
    if (m_finished || !m_accepted || m_reconnecting || m_pumping)
        return;
    m_pumping = true;
    announce();
    fill();
    m_pumping = false;
    emitProgress(false);
}

void FerrySender::tick()
{
    if (m_canResume && m_accepted && !m_reconnecting && !m_finished && now() - m_lastHeard > kSilenceMs) {
        lost();
        return;
    }
    kick();
    emitProgress(false);
}

// ---------- пульт ----------

void FerrySender::approve(qint64 receiver, bool ok)
{
    sendJson({{QStringLiteral("type"), QStringLiteral("approve")},
              {QStringLiteral("receiver"), double(receiver)},
              {QStringLiteral("ok"), ok}});
}

void FerrySender::approveAll()
{
    m_wantApproval = false;
    sendJson({{QStringLiteral("type"), QStringLiteral("approve")},
              {QStringLiteral("ok"), true},
              {QStringLiteral("all"), true}});
}

void FerrySender::setPolicy(const QVariantMap &policy)
{
    if (policy.contains(QStringLiteral("require_approval")))
        m_wantApproval = policy.value(QStringLiteral("require_approval")).toBool();
    QJsonObject o = QJsonObject::fromVariantMap(policy);
    o.insert(QStringLiteral("type"), QStringLiteral("policy"));
    sendJson(o);
}

// Уход и отзыв дублируются по HTTP: в сокете они встают за мегабайтами
// чанков и могут не успеть, а отдельный запрос — успевает.
void FerrySender::beacon(const char *what)
{
    if (m_id.isEmpty() || m_ownerToken.isEmpty() || !m_nam)
        return;
    QNetworkRequest req = FerryApi::request(
        m_s.origin, QStringLiteral("/api/transfers/%1/%2").arg(m_id, QString::fromLatin1(what)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("text/plain"));
    QNetworkReply *reply = m_nam->post(req, m_ownerToken.toLatin1());
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void FerrySender::stop(bool revoke)
{
    if (m_finished)
        return;
    if (m_accepted) {
        if (revoke && m_features.contains(QLatin1String(ferry::kFeaturePolicy))) {
            sendJson({{QStringLiteral("type"), QStringLiteral("revoke")}});
            beacon("revoke");
        } else if (!revoke) {
            sendJson({{QStringLiteral("type"), QStringLiteral("bye")}});
            beacon("leave");
        }
    }
    m_finished = true;
    if (m_tick)
        m_tick->stop();
    if (m_hasher)
        m_hasher->cancel();
    if (QWebSocket *ws = m_ws) {
        m_ws = nullptr;
        ws->disconnect(this);
        connect(ws, &QWebSocket::disconnected, ws, &QObject::deleteLater);
        QTimer::singleShot(3000, ws, &QObject::deleteLater);
        ws->close();
    }
    setPhase(revoke ? QStringLiteral("revoked") : QStringLiteral("closed"));
    emitProgress(true);
    emit closed();
}

void FerrySender::fail(const QString &code, const QString &message)
{
    if (m_finished)
        return;
    m_finished = true;
    if (m_tick)
        m_tick->stop();
    if (m_hasher)
        m_hasher->cancel();
    if (QWebSocket *ws = m_ws) {
        m_ws = nullptr;
        ws->disconnect(this);
        ws->abort();
        ws->deleteLater();
    }
    setPhase(QStringLiteral("failed"));
    emitProgress(true);
    emit failed(code, message);
}

void FerrySender::setPhase(const QString &phase)
{
    if (m_phase == phase)
        return;
    m_phase = phase;
    emitProgress(true);
}

void FerrySender::emitProgress(bool force)
{
    const qint64 t = now();
    if (!force && t - m_lastProgress < kProgressMs)
        return;
    m_lastProgress = t;
    emit progress({
        {QStringLiteral("phase"), m_phase},
        {QStringLiteral("hashed"), double(m_hashed)},
        {QStringLiteral("count"), double(m_plan.chunkCount)},
        {QStringLiteral("hashesDone"), m_hashesDoneSent},
        {QStringLiteral("hashRate"), m_hashMeter.read(t)},
        {QStringLiteral("sentBytes"), double(m_sentBytes)},
        {QStringLiteral("distinctBytes"), double(m_distinctBytes)},
        {QStringLiteral("rate"), m_meter.read(t)},
        {QStringLiteral("receivers"), m_receivers},
    });
}
