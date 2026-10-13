#include "FerryHub.h"

#include <QDir>
#include <QNetworkAccessManager>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include "FerryTransfer.h"
#include "core/Link.h"
#include "../net/ServerInfoController.h"
#include "../net/SignalingClient.h"

#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

FerryHub::FerryHub(ServerInfoController *server, SignalingClient *conf, QObject *parent)
    : QObject(parent), m_server(server), m_conf(conf), m_nam(new QNetworkAccessManager(this))
{
    connect(m_conf, &SignalingClient::left, this, &FerryHub::onLeftCall);
    // Сервер назвал свой релей (или его сменили) — карточкам в тумане с этого
    // релея больше незачем ждать нажатия.
    connect(m_server, &ServerInfoController::changed, this, [this] {
        for (FerryTransfer *t : std::as_const(m_transfers)) {
            if (t->phase() == QLatin1String("fog") && isTrusted(t->origin()))
                t->probe(false);
        }
    });
}

FerryHub::~FerryHub()
{
    shutdown();
}

QString FerryHub::downloadDir() const
{
    const QString saved = QSettings().value(QStringLiteral("ferry/downloadDir")).toString();
    if (!saved.isEmpty() && QDir(saved).exists())
        return saved;
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

void FerryHub::setDownloadDir(const QString &dir)
{
    // Из FolderDialog приходит file:///…; храним обычный путь.
    const QString path = dir.startsWith(QLatin1String("file:")) ? QUrl(dir).toLocalFile() : dir;
    QSettings().setValue(QStringLiteral("ferry/downloadDir"), QDir::cleanPath(path));
    emit settingsChanged();
}

QString FerryHub::seedMode() const
{
    const QString m = QSettings().value(QStringLiteral("ferry/seedMode")).toString();
    return m == QLatin1String("app") || m == QLatin1String("off") ? m : QStringLiteral("call");
}

void FerryHub::setSeedMode(const QString &mode)
{
    QSettings().setValue(QStringLiteral("ferry/seedMode"), mode);
    emit settingsChanged();
    // «Не раздавать» — сразу для всех: человек имел в виду и то, что идёт.
    if (mode == QLatin1String("off")) {
        for (FerryTransfer *t : std::as_const(m_transfers)) {
            if (t->seeding())
                t->setSeeding(false);
        }
    }
}

bool FerryHub::seedByDefault() const
{
    return seedMode() != QLatin1String("off");
}

QStringList FerryHub::trustedRelays() const
{
    return QSettings().value(QStringLiteral("ferry/trusted")).toStringList();
}

QString FerryHub::normOrigin(const QString &origin)
{
    const QUrl u(origin);
    if (!u.isValid() || u.host().isEmpty())
        return origin.toLower();
    QString out = u.scheme().toLower() + QStringLiteral("://") + u.host().toLower();
    const int port = u.port();
    const bool defaultPort = port < 0 || (u.scheme() == QLatin1String("https") && port == 443)
                             || (u.scheme() == QLatin1String("http") && port == 80);
    if (!defaultPort)
        out += QLatin1Char(':') + QString::number(port);
    return out;
}

bool FerryHub::isTrusted(const QString &origin) const
{
    const QString n = normOrigin(origin);
    const QString own = m_server->ferryRelay();
    if (!own.isEmpty() && normOrigin(own) == n)
        return true;
    return trustedRelays().contains(n);
}

void FerryHub::trust(const QString &origin)
{
    QStringList list = trustedRelays();
    const QString n = normOrigin(origin);
    if (list.contains(n))
        return;
    list.append(n);
    QSettings().setValue(QStringLiteral("ferry/trusted"), list);
    emit settingsChanged();
}

void FerryHub::forgetTrusted()
{
    QSettings().remove(QStringLiteral("ferry/trusted"));
    emit settingsChanged();
}

bool FerryHub::isFerryLink(const QString &url) const
{
    // Сначала дешёвая форма, потом разбор ядром — тем же, что у CLI.
    static const QRegularExpression rx(
        QStringLiteral("^https?://[^/\\s#?]+/t/[A-Za-z0-9_-]{22}/?(#[A-Za-z0-9_-]*)?$"));
    if (!rx.match(url).hasMatch())
        return false;
    ferry::TransferLink link;
    return ferry::TransferLink::parse(url.toStdString(), link);
}

QObject *FerryHub::transfer(const QString &url)
{
    ferry::TransferLink link;
    if (!isFerryLink(url) || !ferry::TransferLink::parse(url.toStdString(), link))
        return nullptr;
    const QString origin = normOrigin(QString::fromStdString(link.origin()));
    const QString id = QString::fromStdString(link.id);
    const QString key = origin + QLatin1Char('|') + id;

    FerryTransfer *t = m_transfers.value(key);
    if (!t) {
        t = new FerryTransfer(this, origin, id, this);
        // Карточки приходят и уходят вместе с прокруткой ленты, а раздача
        // живёт дальше: QML не должен её удалять.
        QQmlEngine::setObjectOwnership(t, QQmlEngine::CppOwnership);
        m_transfers.insert(key, t);
        connect(t, &FerryTransfer::changed, this, &FerryHub::recount);
        connect(t, &FerryTransfer::progressChanged, this, &FerryHub::recount);
        connect(t, &FerryTransfer::finished, this, [this, t] { emit transferFinished(t->name()); });
        if (link.hasKey)
            t->adoptKey(link.key);
        t->begin();
    } else if (link.hasKey) {
        t->adoptKey(link.key);
    }
    return t;
}

void FerryHub::recount()
{
    int n = 0;
    for (FerryTransfer *t : std::as_const(m_transfers))
        n += t->active() ? 1 : 0;
    if (n != m_active) {
        m_active = n;
        emit activeChanged();
    }
}

void FerryHub::reveal(const QString &path)
{
    if (path.isEmpty())
        return;
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(native.c_str())) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
    if (SUCCEEDED(co))
        CoUninitialize();
}

// Карточке путь нужен узнаваемым, а не полным: «Загрузки / том», а для
// своей папки — её хвост, «… / проекты / том». Целиком путь виден в
// настройках и в проводнике по «Открыть папку».
QString FerryHub::displayPath(const QString &path) const
{
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (path == downloads)
        return QStringLiteral("Загрузки");
    if (path.startsWith(downloads + QLatin1Char('/'))) {
        QString rest = path.mid(downloads.size() + 1);
        return QStringLiteral("Загрузки / ") + rest.replace(QLatin1Char('/'), QStringLiteral(" / "));
    }
    const QStringList parts = QDir::cleanPath(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() <= 3)
        return parts.join(QStringLiteral(" / "));
    return QStringLiteral("… / ") + parts.mid(parts.size() - 2).join(QStringLiteral(" / "));
}

QThread *FerryHub::ioThread()
{
    if (!m_io) {
        m_io = new QThread(this);
        m_io->setObjectName(QStringLiteral("ferry-io"));
        m_io->start();
    }
    return m_io;
}

QString FerryHub::myName() const
{
    return m_conf->myName();
}

QString FerryHub::stateDir() const
{
    // Local, а не Roaming: в состоянии пути этой машины, другой они ни к чему.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/ferry");
    QDir().mkpath(dir);
    return dir;
}

// Вышли из звонка. Раздавать скачанное по умолчанию — пока в звонке: человек
// видит, что его канал занят, только пока смотрит на ленту.
void FerryHub::onLeftCall()
{
    if (seedMode() != QLatin1String("call"))
        return;
    for (FerryTransfer *t : std::as_const(m_transfers)) {
        if (t->phase() == QLatin1String("done") && t->seeding())
            t->setSeeding(false);
    }
}

void FerryHub::shutdown()
{
    for (FerryTransfer *t : std::as_const(m_transfers))
        t->shutdown();
    if (m_io) {
        m_io->quit();
        m_io->wait(5000);
        m_io = nullptr;
    }
}
