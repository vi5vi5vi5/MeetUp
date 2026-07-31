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
#include "net/ChatImages.h"
#include "net/PersonalRoomController.h"
#include "net/LinkController.h"
#include "SysBridge.h"
#include "HistoryStore.h"
#include "MediaSettings.h"
#include "ScreenSources.h"
#include "GlobalHotkeys.h"

#include "media/AudioEngine.h"
#include "media/VideoEngine.h"
#include "media/MediaStats.h"
#include "media/UiSounds.h"
#include "crypto/E2eCipher.h"
#include "crypto/E2eController.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    app.setOrganizationName("MeetUp");
    app.setApplicationName("MeetUp");
    app.setApplicationDisplayName("MeetUp");
    // Номер приходит из CMake (project VERSION) — QML читает его как
    // Qt.application.version в разделе настроек «О программе».
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
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
    // Ключ комнаты. Объявлен раньше всех, кто шифрует: один на приложение и
    // потокобезопасен — им пользуются сразу пять потоков медиа.
    E2eCipher cipher;
    // Картинки чата. Объявлены здесь, но владеет ими движок QML (провайдеры
    // удаляются вместе с ним) — поэтому ниже addImageProvider отдаёт новый
    // объект, а этот указатель нужен только SignalingClient'у.
    auto* chatImages = new ChatImages;
    SignalingClient conf(&api, &cipher, chatImages);
    PersonalRoomController myRoom(&api);   // тот же api -> та же сессия
    SysBridge sys(&api);                   // буфер обмена + ссылки
    // Ссылка-приглашение целиком: код комнаты, ключ E2E и смена сервера.
    // Создаётся после sys/auth/rooms — держит их указатели.
    LinkController link(&api, &auth, &rooms, &sys);
    HistoryStore history;                  // локальная история комнат (QSettings)
    MediaSettings av;                      // устройства/громкость/качество (QSettings)
    ScreenSources screens;                 // мониторы и окна для демонстрации
    GlobalHotkeys hotkeys(&av);            // системные бинды (работают вне фокуса)
    MediaStats stats(&conf);               // счётчики для раздела «Диагностика»
    AudioEngine audio(&conf, &av, &screens, &stats, &cipher);
    // audio даёт часы звука (синхронизация губ) и «замки» голосовой полосы
    VideoEngine video(&conf, &av, &screens, &audio, &stats, &cipher);
    E2eController crypto(&cipher, &conf, &link, &sys);   // ключ: фраза и ссылка
    // Звуки интерфейса. Держит av (выключатель и устройство вывода) и audio —
    // уведомления молчат, когда снят звук конференции.
    UiSounds sfx(&av, &audio);

    // Движок QML объявлен ПОСЛЕ screens: провайдер миниатюр принадлежит
    // движку и держит указатель на screens, а разрушается движок первым.
    QQmlApplicationEngine engine;
    engine.addImageProvider("screengrab", new ScreenThumbProvider(&screens));
    engine.addImageProvider("chatimg", chatImages);   // движок забирает владение
    // Кладём объект в глобальный контекст QML под именем "Auth".
    // Теперь в любом .qml доступно Auth.login(...), Auth.busy, Auth.errorText.
    engine.rootContext()->setContextProperty("Auth", &auth);
    engine.rootContext()->setContextProperty("Rooms", &rooms);
    engine.rootContext()->setContextProperty("Conf", &conf);
    engine.rootContext()->setContextProperty("MyRoom", &myRoom);
    engine.rootContext()->setContextProperty("Sys", &sys);
    engine.rootContext()->setContextProperty("Link", &link);
    engine.rootContext()->setContextProperty("History", &history);
    // Имя "Video" занято QML-типом из QtMultimedia — поэтому Media.
    engine.rootContext()->setContextProperty("Media", &video);
    engine.rootContext()->setContextProperty("AV", &av);
    engine.rootContext()->setContextProperty("Screens", &screens);
    // Аудиодвижок виден QML только ради «общего звука» (deafen) — Audio.
    engine.rootContext()->setContextProperty("Audio", &audio);
    // Числа для раздела «Диагностика» — Stats.
    engine.rootContext()->setContextProperty("Stats", &stats);
    engine.rootContext()->setContextProperty("Hotkeys", &hotkeys);
    // Сквозное шифрование: фраза-ключ, ссылка с ключом — Crypto.
    engine.rootContext()->setContextProperty("Crypto", &crypto);
    // Звуки интерфейса — Sfx.play("toggle-on") и далее по каталогу.
    engine.rootContext()->setContextProperty("Sfx", &sfx);

    

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("MeetUp", "Main");

    return app.exec(); 
}
