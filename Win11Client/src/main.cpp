// MeetUp Win11 client — application entry point.


#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include "net/ApiClient.h"
#include "net/AuthController.h"
#include "net/RoomController.h"
#include "net/SignalingClient.h"
#include "net/PersonalRoomController.h"
#include "SysBridge.h"
#include "HistoryStore.h"
#include "MediaSettings.h"
#include "ScreenSources.h"

#include "media/AudioEngine.h"
#include "media/VideoEngine.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    app.setOrganizationName("MeetUp");
    app.setApplicationName("MeetUp");
    app.setApplicationDisplayName("MeetUp");
    QQuickStyle::setStyle("Basic");

    // QUrl «красиво» декодирует punycode-домены (мит-ап.рф) обратно в юникод,
    // а QWebSocket берёт хост для Host-заголовка ровно из url.host() —
    // кириллица уезжала в хендшейк, и nginx не находил виртуальный хост
    // (проверено wsprobe-репродюсером). Пустой IDN-whitelist заставляет QUrl
    // держать хосты в ACE (xn--…) во всём приложении; человекочитаемый вид
    // для интерфейса собирает SysBridge из строки адреса.
    QUrl::setIdnWhitelist(QStringList());

    // Транспорт и контроллеры живут всё приложение (стек main).
    ApiClient api;
    AuthController auth(&api);
    RoomController rooms(&api);
    SignalingClient conf(&api);
    PersonalRoomController myRoom(&api);   // тот же api -> та же сессия
    SysBridge sys(&api);                   // буфер обмена + ссылки
    HistoryStore history;                  // локальная история комнат (QSettings)
    MediaSettings av;                      // устройства/громкость/качество (QSettings)
    ScreenSources screens;                 // мониторы и окна для демонстрации
    AudioEngine audio(&conf, &av);
    VideoEngine video(&conf, &av, &screens, &audio);   // audio даёт часы звука

    // Движок QML объявлен ПОСЛЕ screens: провайдер миниатюр принадлежит
    // движку и держит указатель на screens, а разрушается движок первым.
    QQmlApplicationEngine engine;
    engine.addImageProvider("screengrab", new ScreenThumbProvider(&screens));
    // Кладём объект в глобальный контекст QML под именем "Auth".
    // Теперь в любом .qml доступно Auth.login(...), Auth.busy, Auth.errorText.
    engine.rootContext()->setContextProperty("Auth", &auth);
    engine.rootContext()->setContextProperty("Rooms", &rooms);
    engine.rootContext()->setContextProperty("Conf", &conf);
    engine.rootContext()->setContextProperty("MyRoom", &myRoom);
    engine.rootContext()->setContextProperty("Sys", &sys);
    engine.rootContext()->setContextProperty("History", &history);
    // Имя "Video" занято QML-типом из QtMultimedia — поэтому Media.
    engine.rootContext()->setContextProperty("Media", &video);
    engine.rootContext()->setContextProperty("AV", &av);
    engine.rootContext()->setContextProperty("Screens", &screens);

    

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("MeetUp", "Main");

    return app.exec();
}
