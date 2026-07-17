// MeetUp Win11 client — application entry point.


#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include "net/ApiClient.h"
#include "net/AuthController.h"
#include "net/RoomController.h"
#include "net/SignalingClient.h"
#include "net/PersonalRoomController.h"
#include "SysBridge.h"
#include "HistoryStore.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    app.setOrganizationName("MeetUp");
    app.setApplicationName("MeetUp");
    app.setApplicationDisplayName("MeetUp");
    QQuickStyle::setStyle("Basic");

    // Транспорт и контроллеры живут всё приложение (стек main).
    ApiClient api;
    AuthController auth(&api);
    RoomController rooms(&api);
    SignalingClient conf(&api);
    PersonalRoomController myRoom(&api);   // тот же api -> та же сессия
    SysBridge sys(&api);                   // буфер обмена + ссылки
    HistoryStore history;                  // локальная история комнат (QSettings)

    QQmlApplicationEngine engine;
    // Кладём объект в глобальный контекст QML под именем "Auth".
    // Теперь в любом .qml доступно Auth.login(...), Auth.busy, Auth.errorText.
    engine.rootContext()->setContextProperty("Auth", &auth);
    engine.rootContext()->setContextProperty("Rooms", &rooms);
    engine.rootContext()->setContextProperty("Conf", &conf);
    engine.rootContext()->setContextProperty("MyRoom", &myRoom);
    engine.rootContext()->setContextProperty("Sys", &sys);
    engine.rootContext()->setContextProperty("History", &history);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("MeetUp", "Main");

    return app.exec();
}
