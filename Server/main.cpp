#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>

#include <memory>

#include "core/RoomRegistry.h"
#include "interface/InMemory/InMemoryPersonalRooms.h"
#include "interface/InMemory/InMemorySessions.h"
#include "interface/InMemory/InMemoryUsers.h"
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

    parser.addOption(wsPortOption);
    parser.addOption(httpPortOption);
    parser.addOption(webRootOption);
    parser.process(app);

    const quint16 wsPort = parser.value(wsPortOption).toUShort();
    const quint16 httpPort = parser.value(httpPortOption).toUShort();

    QString webRoot = parser.value(webRootOption);
    if (webRoot.isEmpty())
        webRoot = QDir(app.applicationDirPath()).filePath(QStringLiteral("web"));

    // Общий реестр комнат: HTTP API создаёт комнаты, WebSocket-relay
    // подключает в них участников.
    RoomRegistry registry;

    // Хранилища и сервисы (как в MedFlow: репозитории → сервисы → серверы).
    // Пока InMemory — данные живут до перезапуска; SQLite подключится позже
    // заменой этих двух строк, интерфейсы и сервисы не изменятся.
    auto users = std::make_shared<InMemoryUsers>();
    auto sessions = std::make_shared<InMemorySessions>();
    auto personalRooms = std::make_shared<InMemoryPersonalRooms>();
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
