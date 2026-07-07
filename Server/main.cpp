#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>

#include "network/ConferenceServer.h"
#include "network/HttpFileServer.h"

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

    ConferenceServer conference(wsPort);
    HttpFileServer http(httpPort, webRoot);

    if (!conference.isListening() || !http.isListening())
        return 1;

    qInfo().noquote() << QStringLiteral("Ready. Open http://localhost:%1 in your browser.").arg(httpPort);
    return app.exec();
}
