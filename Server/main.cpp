#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>

#include <memory>

#include "config/Log.h"
#include "config/ServerConfig.h"
#include "core/RoomRegistry.h"
#include "interface/sqlite/SqliteDb.h"
#include "interface/sqlite/SqlitePersonalRooms.h"
#include "interface/sqlite/SqliteRoomAliases.h"
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
        QStringLiteral("Directory with the web client (entry point login.html)."),
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

    // Настройки лежат рядом с базой, в той же примонтированной папке, поэтому
    // читаются сразу же, как только известен dataDir. Первое, что делаем с
    // прочитанным, — ставим обработчик журнала: всё, что случится дальше,
    // должно печататься уже по правилам из конфига, а не по умолчанию Qt.
    const ServerConfig cfg = ServerConfig::load(dataDir);
    Log::install(cfg.logLevel);

    qCInfo(lcApp).noquote()
        << QStringLiteral("MeetUp %1 · сборка %2%3")
               .arg(QCoreApplication::applicationVersion(), ServerConfig::buildCommit(),
                    ServerConfig::buildModified() ? QStringLiteral(" (с локальными изменениями)")
                                                  : QString());
    if (cfg.createdNow)
        qCInfo(lcApp).noquote() << QStringLiteral("Создан файл настроек %1").arg(cfg.path);
    // Про опечатки в конфиге говорим вслух и на уровне errors: настройка,
    // которая молча не применилась, хуже настройки, которой нет.
    for (const QString &warning : cfg.warnings)
        qWarning().noquote() << warning;

    // Общий реестр комнат: HTTP API создаёт комнаты, WebSocket-relay
    // подключает в них участников. Объявлен ДО серверов: они держат сырой
    // указатель на него, и умереть он должен последним.
    RoomRegistry registry(ChatLimits{cfg.chatHistorySize, cfg.chatHistoryImages},
                          cfg.maxTotalRooms);

    // Хранилища и сервисы (как в MedFlow: репозитории → сервисы → серверы).
    // Репозитории на SQLite; InMemory-реализации остаются для тестов и как
    // образец — интерфейсы у них общие.
    //
    // Сервисы получают не конфиг, а готовые числа: правила не должны знать,
    // из какого файла они взялись. Перевод «настройка -> предел» — здесь.
    auto db = std::make_shared<SqliteDb>(QDir(dataDir).filePath(QStringLiteral("meetup.db")));
    if (!db->isOpen())
        return 1;
    // Миграция схемы сорвалась. База при этом цела — всё шло в транзакции, —
    // но продолжать нельзя: половина кода уже рассчитывает на новую схему.
    if (db->migrationFailed())
        return 1;
    auto users = std::make_shared<SqliteUsers>(db);
    auto sessions = std::make_shared<SqliteSessions>(db);
    auto personalRooms = std::make_shared<SqlitePersonalRooms>(db);
    auto roomAliases = std::make_shared<SqliteRoomAliases>(db);
    auto auth = std::make_shared<AuthService>(
        users, sessions,
        AuthLimits{qint64(cfg.sessionTtlDays) * 24 * 3600 * 1000,
                   cfg.minPasswordLen, cfg.pbkdf2Iters});
    auto rooms = std::make_shared<PersonalRoomService>(
        personalRooms, roomAliases,
        RoomLimits{cfg.codeMinLen, cfg.maxAliasesPerRoom, cfg.maxPersonalPerUser});

    HttpApi api(auth, rooms, &registry, dataDir, cfg);
    ConferenceServer conference(wsPort, &registry, auth, rooms, cfg);
    HttpFileServer http(httpPort, webRoot, &api, cfg.webEnabled);

    if (!conference.isListening() || !http.isListening())
        return 1;

    // Протухшие сессии чистим раз в час; актуальность конкретной сессии
    // AuthService и так проверяет при каждом обращении.
    QTimer purgeSessionsTimer;
    QObject::connect(&purgeSessionsTimer, &QTimer::timeout,
                     [&auth] { auth->purgeExpiredSessions(); });
    purgeSessionsTimer.start(60 * 60 * 1000);

    QStringList closed;
    if (!cfg.registrationOpen)
        closed << QStringLiteral("регистрация закрыта");
    if (!cfg.allowAnonymousJoin)
        closed << QStringLiteral("вход без аккаунта запрещён");
    if (cfg.anonymousCreate == ServerConfig::RoomCreate::Account)
        closed << QStringLiteral("разовые комнаты — только вошедшим");
    else if (cfg.anonymousCreate == ServerConfig::RoomCreate::Off)
        closed << QStringLiteral("разовые комнаты выключены");
    if (cfg.maxTotalRooms > 0)
        closed << QStringLiteral("потолок разовых комнат: %1").arg(cfg.maxTotalRooms);
    if (cfg.maxPersonalPerUser != 1)
        closed << QStringLiteral("личных комнат на человека: %1").arg(cfg.maxPersonalPerUser);
    if (cfg.maxScreenShares != 1)
        closed << QStringLiteral("демонстраций в комнате: %1").arg(cfg.maxScreenShares);
    if (!closed.isEmpty())
        qCInfo(lcApp).noquote() << QStringLiteral("Ограничения: %1").arg(closed.join(
            QStringLiteral("; ")));

    if (cfg.webEnabled)
        qCInfo(lcApp).noquote()
            << QStringLiteral("Готово. Откройте http://localhost:%1").arg(httpPort);
    else
        qCInfo(lcApp).noquote()
            << QStringLiteral("Готово. Веб-клиент выключен — сервер отвечает только "
                              "по /api и WebSocket.");
    return app.exec();
}
