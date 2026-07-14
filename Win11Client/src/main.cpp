// MeetUp Win11 client — application entry point.
//
// M0: boots the QML UI shell (dark "Prism Minimal" theme, lobby + conference
// screens on mock data). Networking, media and E2E arrive in later milestones
// (see docs/ROADMAP.md).

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QIcon>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    app.setOrganizationName("MeetUp");
    app.setApplicationName("MeetUp");
    app.setApplicationDisplayName("MeetUp");

    // Use the Basic style so our custom dark theme is not overridden by the
    // native Windows look on Controls we do use (StackView, TextField, TabBar).
    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("MeetUp", "Main");

    return app.exec();
}
