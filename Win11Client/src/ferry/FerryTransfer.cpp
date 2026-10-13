#include "FerryTransfer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QThreadPool>
#include <QUrl>
#include <QtAlgorithms>

#include "Cli/Password.h"
#include "FerryHub.h"
#include "FerryReceiver.h"

namespace {

// Имя недокачки рядом с будущим томом: «<имя>.ferry-part», а занято (чужой
// файл с таким именем или вторая раздача того же тома) — «<имя> (2).ferry-part».
QString uniquePart(const QString &dir, const QString &name)
{
    QString candidate = dir + QLatin1Char('/') + name + QStringLiteral(".ferry-part");
    for (int i = 2; QFileInfo::exists(candidate) && i < 10000; ++i)
        candidate = dir + QLatin1Char('/') + name + QStringLiteral(" (%1).ferry-part").arg(i);
    return candidate;
}

} // namespace

FerryTransfer::FerryTransfer(FerryHub *hub, const QString &origin, const QString &id, QObject *parent)
    : QObject(parent), m_hub(hub), m_origin(origin), m_id(id)
{
    loadSaved();
}

FerryTransfer::~FerryTransfer() = default;

QString FerryTransfer::host() const
{
    return m_origin.section(QStringLiteral("://"), 1);
}

QString FerryTransfer::link() const
{
    QString s = m_origin + QStringLiteral("/t/") + m_id;
    if (m_hasKey && !m_meta.password)
        s += QLatin1Char('#') + QString::fromLatin1(
                 QByteArray(reinterpret_cast<const char *>(m_key.data()), int(m_key.size()))
                     .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    return s;
}

QString FerryTransfer::name() const
{
    return m_hasVolume ? QString::fromStdString(m_volume.manifest.name) : m_savedName;
}

QString FerryTransfer::stateFile() const
{
    const QByteArray h = QCryptographicHash::hash((m_origin + QLatin1Char('|') + m_id).toUtf8(),
                                                  QCryptographicHash::Sha1).toHex().left(20);
    return m_hub->stateDir() + QStringLiteral("/recv-") + QString::fromLatin1(h) + QStringLiteral(".json");
}

bool FerryTransfer::active() const
{
    if (!m_rx)
        return false;
    if (m_phase == QLatin1String("done"))
        return m_seeding;
    return m_phase == QLatin1String("connecting") || m_phase == QLatin1String("approval")
           || m_phase == QLatin1String("receiving") || m_phase == QLatin1String("reconnecting");
}

// То, что осталось от прошлых запусков: готовый том или недокачка. Ключа
// там нет и не бывает — он в ссылке, то есть в истории чата.
void FerryTransfer::loadSaved()
{
    QFile f(stateFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value(QStringLiteral("v")).toInt() != 1 || o.value(QStringLiteral("id")).toString() != m_id)
        return;
    m_savedName = o.value(QStringLiteral("name")).toString();
    m_savedKind = o.value(QStringLiteral("kind")).toString();
    m_savedTotal = o.value(QStringLiteral("total")).toDouble();
    const QString final = o.value(QStringLiteral("final")).toString();
    const QString part = o.value(QStringLiteral("part")).toString();
    if (o.value(QStringLiteral("complete")).toBool() && !final.isEmpty() && QFileInfo::exists(final)) {
        m_finalPath = final;
        m_bytes = m_savedTotal;
        m_phase = QStringLiteral("done");
        return;
    }
    if (!o.value(QStringLiteral("complete")).toBool() && !part.isEmpty() && QFileInfo::exists(part)) {
        m_partial = true;
        m_savedPart = part;
        m_savedDir = o.value(QStringLiteral("dir")).toString();
        // Сколько уже лежит — по карте: точнее не нужно, это полоса в карточке.
        const QByteArray bits = QByteArray::fromBase64(o.value(QStringLiteral("bits")).toString().toLatin1(),
                                                       QByteArray::Base64UrlEncoding);
        quint64 have = 0;
        for (char c : bits)
            have += quint64(qPopulationCount(quint8(c)));
        m_count = o.value(QStringLiteral("count")).toDouble();
        m_bytes = std::min(m_savedTotal, double(have) * o.value(QStringLiteral("chunkSize")).toDouble());
    }
}

void FerryTransfer::adoptKey(const ferry::Key32 &key)
{
    if (m_hasKey)
        return;
    m_key = key;
    m_hasKey = true;
    if (m_phase == QLatin1String("nokey"))
        probe(false);
    emit changed();
}

void FerryTransfer::begin()
{
    if (m_phase == QLatin1String("done"))
        return;
    if (m_hub->isTrusted(m_origin))
        probe(false);
    else
        setPhase(QStringLiteral("fog"));
}

void FerryTransfer::probe(bool remember)
{
    if (remember)
        m_hub->trust(m_origin);
    if (m_probe)
        return;
    if (m_phase != QLatin1String("done") && !m_rx)
        setPhase(QStringLiteral("probing"));
    QNetworkRequest req = FerryApi::request(m_origin, QStringLiteral("/api/transfers/") + m_id);
    // Умеем хеши на лету: иначе раздача, у которой отправитель ещё считает,
    // ответила бы «зайдите через минуту».
    req.setRawHeader(ferry::kFeaturesHeader, ferry::kFeatureStreamHashes);
    QNetworkReply *reply = m_hub->nam()->get(req);
    m_probe = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onMeta(reply); });
}

void FerryTransfer::onMeta(QNetworkReply *reply)
{
    reply->deleteLater();
    m_probe = nullptr;
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const After after = m_after;
    m_after = After::Nothing;

    if (status != 200) {
        const QString code = FerryApi::errorCode(reply, body);
        // Том уже на диске — что бы ни думал релей, карточка остаётся «доставлено».
        if (m_phase == QLatin1String("done")) {
            if (after == After::Seed) {
                m_seeding = false;
                emit progressChanged();
            }
            return;
        }
        setFailed(code, QString(), !FerryApi::isFinal(code));
        return;
    }
    FerryMeta meta;
    if (!FerryMeta::parse(body, meta)) {
        setFailed(QStringLiteral("not_ferry"), QString(), false);
        return;
    }
    m_meta = meta;
    m_hasMeta = true;
    m_relayState = meta.state;
    m_usesLeft = meta.usesLeft;

    if (!m_hasKey) {
        setPhase(meta.password ? QStringLiteral("password") : QStringLiteral("nokey"));
        return;
    }
    m_after = after;
    openWithKey();
}

void FerryTransfer::openWithKey()
{
    const After after = m_after;
    m_after = After::Nothing;
    FerryVolume vol;
    QString code, text;
    if (!FerryVolume::open(m_meta, m_key, vol, &code, &text)) {
        if (m_meta.password && code == QLatin1String("bad_key")) {
            // Ключ из фразы не открыл манифест — значит, фраза не та.
            m_hasKey = false;
            m_passwordError = QStringLiteral("Фраза не подошла — проверьте сами слова.");
            setPhase(QStringLiteral("password"));
            return;
        }
        setFailed(code, text, false);
        return;
    }
    m_volume = std::move(vol);
    m_hasVolume = true;
    m_passwordError.clear();
    if (after == After::Accept) {
        startReceiver(false);
    } else if (after == After::Seed) {
        startReceiver(true);
    } else if (m_phase != QLatin1String("done") && !m_rx) {
        setPhase(QStringLiteral("offer"));
    } else {
        emit changed();
    }
}

void FerryTransfer::accept()
{
    if (m_rx)
        return;
    m_declined = false;
    // Метаданные — заново: с показа карточки могли закончиться хеши на лету,
    // уйти отправитель или кончиться использования.
    m_after = After::Accept;
    if (!m_probe)
        probe(false);
}

void FerryTransfer::startReceiver(bool seedOnly)
{
    FerryReceiver::Setup s;
    s.origin = m_origin;
    s.id = m_id;
    s.meta = m_meta;
    s.volume = m_volume;
    s.stateFile = stateFile();
    s.name = m_hub->myName();
    s.seeding = seedOnly || m_hub->seedByDefault();
    if (seedOnly) {
        s.finalPath = m_finalPath;
        s.finalDir = QFileInfo(m_finalPath).absolutePath();
    } else {
        // Недокачка, начатая раньше (в этот запуск или прошлый), — наша:
        // продолжаем в неё, а не заводим «(2)» рядом.
        const QString dir = !m_savedDir.isEmpty() ? m_savedDir : m_hub->downloadDir();
        const QString safe = QString::fromStdString(ferry::safeFileName(m_volume.manifest.name));
        s.finalDir = dir;
        s.partPath = !m_savedPart.isEmpty() ? m_savedPart : uniquePart(dir, safe);
        m_savedDir = s.finalDir;
        m_savedPart = s.partPath;
    }

    auto *rx = new FerryReceiver(std::move(s));
    rx->moveToThread(m_hub->ioThread());
    connect(rx, &FerryReceiver::progress, this, &FerryTransfer::onProgress);
    connect(rx, &FerryReceiver::failed, this, &FerryTransfer::onFailed);
    connect(rx, &FerryReceiver::done, this, [this](const QString &path) {
        m_finalPath = path;
        m_partial = false;
        m_savedPart.clear();
        emit changed();
        emit finished();
    });
    connect(rx, &FerryReceiver::stopped, this, [this, rx] {
        if (m_rx == rx)
            m_rx = nullptr;
        rx->deleteLater();
        emit changed();
    });
    m_rx = rx;
    m_wasReceiving = !seedOnly;
    m_errorCode.clear();
    if (!seedOnly)
        setPhase(QStringLiteral("connecting"));
    QMetaObject::invokeMethod(rx, &FerryReceiver::start, Qt::QueuedConnection);
}

void FerryTransfer::onProgress(const QVariantMap &s)
{
    m_bytes = s.value(QStringLiteral("bytes")).toDouble();
    m_rate = s.value(QStringLiteral("rate")).toDouble();
    m_count = s.value(QStringLiteral("count")).toDouble();
    m_cells = s.value(QStringLiteral("cells")).toList();
    m_reconnectInMs = s.value(QStringLiteral("reconnectInMs")).toDouble();
    m_senderAway = s.value(QStringLiteral("senderAway")).toBool();
    m_senderWaitMs = s.value(QStringLiteral("senderWaitMs")).toDouble();
    m_seeding = s.value(QStringLiteral("seeding")).toBool();
    m_servedBytes = s.value(QStringLiteral("servedBytes")).toDouble();
    const QString relay = s.value(QStringLiteral("relayState")).toString();
    if (!relay.isEmpty())
        m_relayState = relay;
    const qint64 uses = s.value(QStringLiteral("usesLeft")).toLongLong();
    if (uses != -2)
        m_usesLeft = uses;
    const QString path = s.value(QStringLiteral("finalPath")).toString();
    if (!path.isEmpty())
        m_finalPath = path;

    const QString ph = s.value(QStringLiteral("phase")).toString();
    if (ph == QLatin1String("stopped"))
        m_partial = m_finalPath.isEmpty();
    if (ph != QLatin1String("failed") && ph != QLatin1String("idle"))
        setPhase(ph);
    emit progressChanged();
}

void FerryTransfer::onFailed(const QString &code, const QString &message, bool retry)
{
    if (m_bytes > 0 && m_finalPath.isEmpty())
        m_partial = true;
    setFailed(code, message, retry);
}

void FerryTransfer::setFailed(const QString &code, const QString &detail, bool retry)
{
    const FerryApi::Refusal r = FerryApi::describe(code);
    m_errorCode = code;
    m_errorTitle = r.title;
    m_errorText = detail.isEmpty() ? r.text : detail;
    m_errorWay = r.way;
    m_retryable = retry;
    setPhase(QStringLiteral("failed"));
    emit changed();
}

void FerryTransfer::setPhase(const QString &phase)
{
    if (m_phase == phase)
        return;
    m_phase = phase;
    emit changed();
}

void FerryTransfer::stop()
{
    if (m_rx)
        QMetaObject::invokeMethod(m_rx, &FerryReceiver::stop, Qt::QueuedConnection);
}

void FerryTransfer::retry()
{
    m_errorCode.clear();
    if (m_rx) {
        // Упавший приёмник стоит без дела — его можно отпускать сразу.
        FerryReceiver *rx = m_rx;
        m_rx = nullptr;
        rx->disconnect(this);
        rx->deleteLater();
    }
    if (m_wasReceiving || m_partial)
        accept();
    else
        probe(false);
}

void FerryTransfer::unlock(const QString &phrase)
{
    if (!m_hasMeta || !m_meta.password || m_meta.salt.size() != int(ferry::kPasswordSaltSize))
        return;
    m_passwordError.clear();
    setPhase(QStringLiteral("unlocking"));
    // Argon2id с 64 МиБ памяти — около секунды: не на GUI-потоке.
    const std::string normalized = ferry::cli::normalizePassword(phrase.toStdString());
    const QByteArray salt = m_meta.salt;
    QPointer<FerryTransfer> self(this);
    QThreadPool::globalInstance()->start([self, normalized, salt] {
        ferry::Key32 key{};
        const bool ok = ferry::cli::passwordKey(normalized, reinterpret_cast<const uint8_t *>(salt.constData()),
                                                size_t(salt.size()), key);
        QMetaObject::invokeMethod(qApp, [self, key, ok] {
            if (self)
                self->onUnlocked(key, ok);
        }, Qt::QueuedConnection);
    });
}

void FerryTransfer::onUnlocked(const ferry::Key32 &key, bool ok)
{
    if (!ok) {
        m_passwordError = QStringLiteral("Не хватило памяти, чтобы вывести ключ из фразы.");
        setPhase(QStringLiteral("password"));
        return;
    }
    m_key = key;
    m_hasKey = true;
    openWithKey();
}

bool FerryTransfer::useKey(const QString &text)
{
    ferry::Key32 key{};
    if (!ferryKeyFromText(text, key))
        return false;
    m_key = key;
    m_hasKey = true;
    probe(false);
    return true;
}

void FerryTransfer::decline(bool on)
{
    m_declined = on;
    emit changed();
}

void FerryTransfer::openFolder()
{
    m_hub->reveal(m_finalPath);
}

void FerryTransfer::openInBrowser()
{
    QDesktopServices::openUrl(QUrl(link()));
}

void FerryTransfer::setSeeding(bool on)
{
    if (m_rx) {
        QMetaObject::invokeMethod(m_rx, [rx = m_rx, on] { rx->setSeeding(on); }, Qt::QueuedConnection);
        return;
    }
    if (!on || m_phase != QLatin1String("done"))
        return;
    // Том собран в прошлый раз: раздавать — значит открыть его ключом и
    // прийти к релею заново.
    m_seeding = true;
    emit progressChanged();
    if (m_hasVolume) {
        startReceiver(true);
    } else {
        m_after = After::Seed;
        probe(false);
    }
}

void FerryTransfer::shutdown()
{
    if (m_probe)
        m_probe->abort();
    if (!m_rx)
        return;
    FerryReceiver *rx = m_rx;
    m_rx = nullptr;
    rx->disconnect(this);
    QMetaObject::invokeMethod(rx, &FerryReceiver::stop, Qt::BlockingQueuedConnection);
    rx->deleteLater();
}
