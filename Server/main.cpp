#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>

#include <memory>

#include "core/RoomRegistry.h"
#include "interface/sqlite/SqliteDb.h"
#include "interface/sqlite/SqlitePersonalRooms.h"
#include "interface/sqlite/SqliteSessions.h"
#include "interface/sqlite/SqliteUsers.h"
#include "network/ConferenceServer.h"
#include "network/HttpApi.h"
#include "network/HttpFileServer.h"
#include "services/AuthService.h"
#include "services/PersonalRoomService.h"

#ifndef WEB_ROOT_DEFAULT
#define WEB_ROOT_DEFAULT ""
#endif

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MeetUpServer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MeetUp — WebSocket relay server for conferences"));
    parser.addHelpOption();
    parser.addVersionOption();

    // WebSocket умолчанию 9000. Переопределяется --ws-port.
    QCommandLineOption wsPortOption(
        QStringList{"p", "ws-port"},
        QStringLiteral("WebSocket relay port (default 9000)."),
        QStringLiteral("port"), QStringLiteral("9000"));

    // HTTP умолчанию 80. Переопределяется --http-port.
    QCommandLineOption httpPortOption(
        QStringList{"http-port"},
        QStringLiteral("HTTP port for the web client (default 80)."),
        QStringLiteral("port"), QStringLiteral("80"));
        
    QCommandLineOption webRootOption(
        QStringList{"w", "web-root"},
        QStringLiteral("Directory with the web client (index.html)."),
        QStringLiteral("dir"), QStringLiteral(WEB_ROOT_DEFAULT));

    // Всё персистентное (SQLite) живёт в одной папке: при запуске в docker
    // web-root указывает в примонтированные исходники, и mount рядом с ним
    // оказывается на хосте — данные переживают пересборку и новый контейнер.
    QCommandLineOption dataDirOption(
        QStringList{"d", "data-dir"},
        QStringLiteral("Directory for persistent data (default: <web-root>/../mount)."),
        QStringLiteral("dir"), QString());

    parser.addOption(wsPortOption);
    parser.addOption(httpPortOption);
    parser.addOption(webRootOption);
    parser.addOption(dataDirOption);
    parser.process(app);

    const quint16 wsPort = parser.value(wsPortOption).toUShort();
    const quint16 httpPort = parser.value(httpPortOption).toUShort();

    QString webRoot = parser.value(webRootOption);
    if (webRoot.isEmpty())
        webRoot = QDir(app.applicationDirPath()).filePath(QStringLiteral("web"));

    // Общий реестр комнат: HTTP API создаёт комнаты, WebSocket-relay
    // подключает в них участников.
    RoomRegistry registry;

    // Папка данных создаётся сама и добавлена в .gitignore — git pull её
    // не трогает. База — один файл mount/meetup.db.
    QString dataDir = parser.value(dataDirOption);
    if (dataDir.isEmpty())
        dataDir = QDir(webRoot).filePath(QStringLiteral("../mount"));
    dataDir = QDir::cleanPath(dataDir);
    if (!QDir().mkpath(dataDir)) {
        qCritical().noquote() << QStringLiteral("Failed to create data dir %1").arg(dataDir);
        return 1;
    }

    // Хранилища и сервисы (как в MedFlow: репозитории → сервисы → серверы).
    // Репозитории на SQLite; InMemory-реализации остаются для тестов и как
    // образец — интерфейсы у них общие.
    auto db = std::make_shared<SqliteDb>(QDir(dataDir).filePath(QStringLiteral("meetup.db")));
    if (!db->isOpen())
        return 1;
    auto users = std::make_shared<SqliteUsers>(db);
    auto sessions = std::make_shared<SqliteSessions>(db);
    auto personalRooms = std::make_shared<SqlitePersonalRooms>(db);
    auto auth = std::make_shared<AuthService>(users, sessions);
    auto rooms = std::make_shared<PersonalRoomService>(personalRooms);

    HttpApi api(auth, rooms, &registry);
    ConferenceServer conference(wsPort, &registry, auth, rooms);
    HttpFileServer http(httpPort, webRoot, &api);

    if (!conference.isListening() || !http.isListening())
        return 1;

    // Протухшие сессии чистим раз в час; актуальность конкретной сессии
    // AuthService и так проверяет при каждом обращении.
    QTimer purgeSessionsTimer;
    QObject::connect(&purgeSessionsTimer, &QTimer::timeout,
                     [&auth] { auth->purgeExpiredSessions(); });
    purgeSessionsTimer.start(60 * 60 * 1000);

    qInfo().noquote() << QStringLiteral("Ready. Open http://localhost:%1 in your browser.").arg(httpPort);
    return app.exec();
}
