#include "FerryOutgoing.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSettings>
#include <QThreadPool>
#include <QUrl>

#include "Cli/Password.h"
#include "FerryHub.h"
#include "FerryVolume.h"
#include "core/Protocol.h"

namespace {

QStringList localPaths(const QVariant &urls)
{
    QStringList out;
    const QVariantList list = urls.canConvert<QVariantList>() ? urls.toList() : QVariantList{urls};
    for (const QVariant &v : list) {
        const QUrl u = v.toUrl();
        const QString p = u.isLocalFile() ? u.toLocalFile() : v.toString();
        if (!p.isEmpty())
            out.append(p);
    }
    return out;
}

} // namespace

// ---------- лист погрузки ----------

FerryDraft::FerryDraft(FerryHub *hub, QObject *parent) : QObject(parent), m_hub(hub)
{
    const QString server = hub->serverRelay();
    const QString own = hub->ownRelay();
    if (!own.isEmpty()) {
        m_relay = own;
        m_relaySource = QStringLiteral("own");
    } else if (!server.isEmpty()) {
        m_relay = server;
        m_relaySource = QStringLiteral("server");
    }
    if (!m_relay.isEmpty())
        probeRelay();
}

void FerryDraft::scan(const QStringList &paths)
{
    m_paths = paths;
    m_scanning = true;
    const int serial = ++m_scanSerial;
    emit changed();
    QPointer<FerryDraft> self(this);
    QThreadPool::globalInstance()->start([self, paths, serial] {
        FerryCargo cargo = FerryCargo::scan(paths);
        QMetaObject::invokeMethod(qApp, [self, serial, cargo = std::move(cargo)]() mutable {
            if (!self || serial != self->m_scanSerial)
                return;
            self->m_cargo = std::move(cargo);
            self->m_error = self->m_cargo.error;
            self->m_scanning = false;
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void FerryDraft::add(const QVariant &urls)
{
    QStringList paths = m_paths;
    for (const QString &p : localPaths(urls)) {
        if (!paths.contains(p))
            paths.append(p);
    }
    scan(paths);
}

QVariantList FerryDraft::items() const
{
    QVariantList out;
    const ferry::Manifest &m = m_cargo.manifest;
    if (!m.isTree()) {
        out.append(QVariantMap{{QStringLiteral("name"), name()}, {QStringLiteral("size"), total()},
                               {QStringLiteral("dir"), false}});
        return out;
    }
    // Одна папка — одной строкой «имя/»; несколько вещей — их верхний
    // уровень с суммарными размерами.
    if (m_paths.size() == 1 && QFileInfo(m_paths.first()).isDir()) {
        out.append(QVariantMap{{QStringLiteral("name"), name() + QLatin1Char('/')},
                               {QStringLiteral("size"), total()},
                               {QStringLiteral("dir"), true},
                               {QStringLiteral("files"), files()}});
        return out;
    }
    QStringList order;
    QHash<QString, double> sizes;
    QHash<QString, bool> dirs;
    for (const ferry::ManifestEntry &e : m.entries) {
        const QString path = QString::fromStdString(e.path);
        const int slash = path.indexOf(QLatin1Char('/'));
        const QString top = slash < 0 ? path : path.left(slash);
        if (!sizes.contains(top))
            order.append(top);
        sizes[top] += double(e.size);
        dirs[top] = dirs.value(top) || slash >= 0 || e.isDir;
    }
    for (const QString &top : order)
        out.append(QVariantMap{{QStringLiteral("name"), dirs.value(top) ? top + QLatin1Char('/') : top},
                               {QStringLiteral("size"), sizes.value(top)},
                               {QStringLiteral("dir"), dirs.value(top)}});
    return out;
}

bool FerryDraft::setRelay(const QString &origin)
{
    QString s = origin.trimmed();
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    if (!s.contains(QLatin1String("://")))
        s.prepend(QStringLiteral("https://"));
    const QUrl u(s, QUrl::StrictMode);
    if (!u.isValid() || u.host().isEmpty()
        || (u.scheme() != QLatin1String("https") && u.scheme() != QLatin1String("http")) || !u.path().isEmpty())
        return false;
    m_relay = u.scheme() + QStringLiteral("://") + u.authority();
    const bool server = m_relay == m_hub->serverRelay();
    m_relaySource = server ? QStringLiteral("server") : QStringLiteral("own");
    // Свой релей помним: человек выбрал его сам, в следующий раз — он же.
    m_hub->setOwnRelay(server ? QString() : m_relay);
    probeRelay();
    return true;
}

// Что релей умеет — до ссылки: потолок одновременных получателей для
// листа, подтверждение и пароль — чтобы не обещать того, чего он не поймёт.
void FerryDraft::probeRelay()
{
    m_relayStatus = QStringLiteral("checking");
    emit relayChanged();
    QNetworkReply *reply = m_hub->nam()->get(FerryApi::request(m_relay, QStringLiteral("/api/health")));
    const QString asked = m_relay;
    connect(reply, &QNetworkReply::finished, this, [this, reply, asked] {
        reply->deleteLater();
        if (asked != m_relay)
            return;
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ferry = o.value(QStringLiteral("product")).toString() == QLatin1String("ferry")
                           || o.contains(QStringLiteral("features"));
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 || !ferry) {
            m_relayStatus = QStringLiteral("bad");
            emit relayChanged();
            return;
        }
        QStringList features;
        for (const QJsonValue &f : o.value(QStringLiteral("features")).toArray())
            features.append(f.toString());
        m_relayMax = qMax(1, o.value(QStringLiteral("max_concurrent")).toInt(8));
        m_relayPolicy = features.contains(QLatin1String(ferry::kFeaturePolicy));
        m_relayPassword = features.contains(QLatin1String(ferry::kFeaturePassword));
        m_relayStatus = QStringLiteral("ok");
        emit relayChanged();
    });
}

QString FerryDraft::passwordProblem(const QString &phrase) const
{
    return QString::fromStdString(
        ferry::cli::passwordProblem(ferry::cli::normalizePassword(phrase.toStdString())));
}

void FerryDraft::send(const QVariantMap &options)
{
    if (m_sending || m_scanning || !m_error.isEmpty() || m_relay.isEmpty())
        return;
    m_sending = true;
    emit changed();
    const QString phrase = options.value(QStringLiteral("password")).toString();
    if (phrase.isEmpty()) {
        launch(options, false, ferry::Key32{}, QByteArray());
        return;
    }
    // Argon2id с 64 МиБ — около секунды: не на GUI-потоке.
    QByteArray salt(int(ferry::kPasswordSaltSize), Qt::Uninitialized);
    ferry::randomBytes(reinterpret_cast<uint8_t *>(salt.data()), size_t(salt.size()));
    const std::string normalized = ferry::cli::normalizePassword(phrase.toStdString());
    QPointer<FerryDraft> self(this);
    QThreadPool::globalInstance()->start([self, normalized, salt, options] {
        ferry::Key32 key{};
        const bool ok = ferry::cli::passwordKey(normalized, reinterpret_cast<const uint8_t *>(salt.constData()),
                                                size_t(salt.size()), key);
        QMetaObject::invokeMethod(qApp, [self, key, ok, salt, options] {
            if (!self)
                return;
            if (!ok) {
                self->m_sending = false;
                self->m_error = QStringLiteral("Не хватило памяти, чтобы вывести ключ из фразы.");
                emit self->changed();
                return;
            }
            self->launch(options, true, key, salt);
        }, Qt::QueuedConnection);
    });
}

void FerryDraft::launch(const QVariantMap &o, bool password, const ferry::Key32 &key, const QByteArray &salt)
{
    FerrySender::Setup s;
    s.origin = m_relay;
    s.manifest = m_cargo.manifest;
    s.sources = m_cargo.sources;
    s.policy.uses = o.value(QStringLiteral("uses"), -1).toLongLong();
    s.policy.ttlMs = qint64(o.value(QStringLiteral("ttlHours"), 24).toDouble() * 3600.0 * 1000.0);
    s.policy.maxConcurrent = o.value(QStringLiteral("maxConcurrent"), 0).toInt();
    s.policy.requireApproval = o.value(QStringLiteral("requireApproval")).toBool();
    s.policy.leaveOpen = o.value(QStringLiteral("leaveOpen")).toBool();
    s.password = password;
    s.passwordKey = key;
    s.salt = salt;
    auto *send = new FerrySend(m_hub, std::move(s), double(m_cargo.files), m_hub);
    m_hub->addOutgoing(send);
    send->start();
    emit sent(send);
}

// ---------- своя раздача ----------

FerrySend::FerrySend(FerryHub *hub, FerrySender::Setup setup, double files, QObject *parent)
    : QObject(parent), m_hub(hub), m_files(files)
{
    m_manifest = setup.manifest;
    m_origin = setup.origin;
    m_password = setup.password;
    m_usesLeft = setup.policy.uses;
    m_requireApproval = setup.policy.requireApproval;
    m_leaveOpen = setup.policy.leaveOpen;
    m_maxConcurrent = setup.policy.maxConcurrent;
    if (setup.policy.ttlMs > 0)
        m_expiresAt = double(QDateTime::currentMSecsSinceEpoch() + setup.policy.ttlMs);

    m_tx = new FerrySender(std::move(setup));
    m_tx->moveToThread(hub->ioThread());
    connect(m_tx, &FerrySender::ready, this,
            [this](const QString &link, const QString &ownerLink, const QString &id, const QString &) {
                m_link = link;
                m_ownerLink = ownerLink;
                m_id = id;
                emit changed();
                emit ready(link);
                fetchMeta();
            });
    connect(m_tx, &FerrySender::progress, this, &FerrySend::onProgress);
    connect(m_tx, &FerrySender::relayState, this, &FerrySend::onState);
    connect(m_tx, &FerrySender::knock, this, [this](qint64 receiver, const QString &name) {
        for (const QVariant &k : std::as_const(m_knocks))
            if (k.toMap().value(QStringLiteral("id")).toLongLong() == receiver)
                return;
        m_knocks.append(QVariantMap{{QStringLiteral("id"), double(receiver)}, {QStringLiteral("name"), name}});
        emit changed();
        emit knocked(double(receiver), name);
    });
    connect(m_tx, &FerrySender::knockGone, this, [this](qint64 receiver) {
        for (int i = 0; i < m_knocks.size(); ++i) {
            if (m_knocks.at(i).toMap().value(QStringLiteral("id")).toLongLong() == receiver) {
                m_knocks.removeAt(i);
                emit changed();
                return;
            }
        }
    });
    connect(m_tx, &FerrySender::failed, this, [this](const QString &code, const QString &message) {
        const FerryApi::Refusal r = FerryApi::describe(code);
        m_errorCode = code;
        m_errorTitle = code == QLatin1String("unsupported") ? QStringLiteral("Релей этого не умеет")
                       : code == QLatin1String("read") ? QStringLiteral("Не читается файл тома")
                                                       : r.title;
        m_errorText = message.isEmpty() ? r.text : message;
        m_phase = QStringLiteral("failed");
        m_closedAt = double(QDateTime::currentMSecsSinceEpoch());
        emit changed();
    });
    connect(m_tx, &FerrySender::closed, this, [this] {
        m_closedAt = double(QDateTime::currentMSecsSinceEpoch());
        m_knocks.clear();
        emit changed();
    });
}

FerrySend::~FerrySend() = default;

void FerrySend::start()
{
    QMetaObject::invokeMethod(m_tx, &FerrySender::start, Qt::QueuedConnection);
}

QString FerrySend::key() const
{
    return m_origin + QLatin1Char('|') + m_id;
}

bool FerrySend::active() const
{
    return m_phase == QLatin1String("creating") || m_phase == QLatin1String("hashing")
           || m_phase == QLatin1String("offering") || m_phase == QLatin1String("live")
           || m_phase == QLatin1String("lost");
}

bool FerrySend::hasSeed() const
{
    if (m_relayState == QLatin1String("delegated"))
        return true;
    for (const QVariant &r : m_receivers)
        if (r.toMap().value(QStringLiteral("role")).toString() == QLatin1String("seed"))
            return true;
    return false;
}

QStringList FerrySend::downloading() const
{
    QStringList out;
    for (const QVariant &r : m_receivers) {
        const QVariantMap m = r.toMap();
        if (m.value(QStringLiteral("role")).toString() != QLatin1String("seed"))
            out.append(m.value(QStringLiteral("name")).toString());
    }
    return out;
}

void FerrySend::onProgress(const QVariantMap &s)
{
    const QString ph = s.value(QStringLiteral("phase")).toString();
    if (!ph.isEmpty() && ph != QLatin1String("idle") && ph != m_phase) {
        m_phase = ph;
        emit changed();
    }
    m_hashed = s.value(QStringLiteral("hashed")).toDouble();
    m_count = s.value(QStringLiteral("count")).toDouble();
    m_hashesDone = s.value(QStringLiteral("hashesDone")).toBool();
    m_sentBytes = s.value(QStringLiteral("sentBytes")).toDouble();
    m_rate = s.value(QStringLiteral("rate")).toDouble();

    // Получатели: имя (то, что они назвали релею, — у MeetUp это имя в
    // звонке), доля тома и роль. Откуда байты — суммой по всем: доли от
    // разных знаменателей сложить было бы нельзя, штуки — можно.
    QVariantList list;
    double w = 0, p = 0, snd = 0;
    bool deliveredChanged = false;
    for (const QVariant &rv : s.value(QStringLiteral("receivers")).toList()) {
        const QVariantMap r = rv.toMap();
        const QVariantMap src = r.value(QStringLiteral("src")).toMap();
        w += src.value(QStringLiteral("window")).toDouble();
        p += src.value(QStringLiteral("peers")).toDouble();
        snd += src.value(QStringLiteral("sender")).toDouble();
        const QString name = r.value(QStringLiteral("name")).toString().trimmed();
        const double progress = r.value(QStringLiteral("progress")).toDouble();
        const QString role = r.value(QStringLiteral("role")).toString();
        list.append(QVariantMap{{QStringLiteral("id"), r.value(QStringLiteral("id"))},
                                {QStringLiteral("name"), name.isEmpty() ? QStringLiteral("без имени") : name},
                                {QStringLiteral("progress"), progress},
                                {QStringLiteral("role"), role}});
        const QString shown = name.isEmpty() ? QStringLiteral("без имени") : name;
        if ((progress >= 0.999 || role == QLatin1String("seed")) && !m_delivered.contains(shown)) {
            m_delivered.append(shown);
            deliveredChanged = true;
        }
    }
    m_receivers = list;
    const double sum = w + p + snd;
    m_srcWindow = sum > 0 ? w / sum : 0;
    m_srcPeers = sum > 0 ? p / sum : 0;
    m_srcSender = sum > 0 ? snd / sum : 0;
    if (deliveredChanged)
        emit changed();
    emit progressChanged();
}

void FerrySend::onState(const QVariantMap &v)
{
    if (v.contains(QStringLiteral("uses_left")))
        m_usesLeft = v.value(QStringLiteral("uses_left")).isNull()
                         ? -1 : v.value(QStringLiteral("uses_left")).toLongLong();
    if (v.contains(QStringLiteral("expires_in_ms")))
        m_expiresAt = double(QDateTime::currentMSecsSinceEpoch()
                             + v.value(QStringLiteral("expires_in_ms")).toLongLong());
    if (v.contains(QStringLiteral("require_approval")))
        m_requireApproval = v.value(QStringLiteral("require_approval")).toBool();
    if (v.contains(QStringLiteral("leave_open")))
        m_leaveOpen = v.value(QStringLiteral("leave_open")).toBool();
    if (v.contains(QStringLiteral("max_concurrent")))
        m_maxConcurrent = v.value(QStringLiteral("max_concurrent")).toInt();
    if (v.contains(QStringLiteral("state")))
        m_relayState = v.value(QStringLiteral("state")).toString();
    emit changed();
}

// Сразу после offer_ok релей ещё не присылал state: срок и остаток
// использований берём из метаданных (эта ручка ничего не сжигает).
void FerrySend::fetchMeta()
{
    QNetworkRequest req = FerryApi::request(m_origin, QStringLiteral("/api/transfers/") + m_id);
    req.setRawHeader(ferry::kFeaturesHeader, ferry::kFeatureStreamHashes);
    QNetworkReply *reply = m_hub->nam()->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.isEmpty())
            return;
        onState(o.toVariantMap());
    });
}

void FerrySend::sendPolicy(const QVariantMap &p)
{
    if (!m_tx)
        return;
    QMetaObject::invokeMethod(m_tx, [tx = m_tx, p] { tx->setPolicy(p); }, Qt::QueuedConnection);
}

void FerrySend::approve(double receiver, bool ok)
{
    if (!m_tx)
        return;
    QMetaObject::invokeMethod(m_tx, [tx = m_tx, receiver, ok] { tx->approve(qint64(receiver), ok); },
                              Qt::QueuedConnection);
    for (int i = 0; i < m_knocks.size(); ++i) {
        if (m_knocks.at(i).toMap().value(QStringLiteral("id")).toLongLong() == qint64(receiver)) {
            m_knocks.removeAt(i);
            break;
        }
    }
    emit changed();
}

void FerrySend::approveAll()
{
    if (!m_tx)
        return;
    QMetaObject::invokeMethod(m_tx, &FerrySender::approveAll, Qt::QueuedConnection);
    m_knocks.clear();
    m_requireApproval = false;
    emit changed();
}

void FerrySend::setUses(double uses)
{
    sendPolicy({{QStringLiteral("uses"), uses < 0 ? QVariant::fromValue(nullptr) : QVariant(qint64(uses))}});
}

void FerrySend::setTtlHours(double hours)
{
    sendPolicy({{QStringLiteral("ttl_ms"), qint64(hours * 3600.0 * 1000.0)}});
}

void FerrySend::setApproval(bool on)
{
    sendPolicy({{QStringLiteral("require_approval"), on}});
}

void FerrySend::setLeaveOpen(bool on)
{
    sendPolicy({{QStringLiteral("leave_open"), on}, {QStringLiteral("on_owner_leave"), QStringLiteral("freeze")}});
}

void FerrySend::revoke()
{
    if (!m_tx)
        return;
    QMetaObject::invokeMethod(m_tx, [tx = m_tx] { tx->stop(true); }, Qt::QueuedConnection);
}

void FerrySend::leave()
{
    if (!m_tx)
        return;
    QMetaObject::invokeMethod(m_tx, [tx = m_tx] { tx->stop(false); }, Qt::QueuedConnection);
}

void FerrySend::shutdown()
{
    if (!m_tx)
        return;
    FerrySender *tx = m_tx;
    m_tx = nullptr;
    tx->disconnect(this);
    if (active())
        QMetaObject::invokeMethod(tx, [tx] { tx->stop(false); }, Qt::BlockingQueuedConnection);
    tx->deleteLater();
}
