#pragma once

#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QJsonObject;
class RoomRegistry;

// Крошечный HTTP-сервер двух ролей:
//   1) статика веб-клиента (web/): страница по http://localhost открывается
//      без установки Python или отдельного веб-сервера;
//   2) JSON API (/api/...): создание комнат и заглушки авторизации.
// Точкой входа служит login.html — «/» отдаёт именно его.
class HttpFileServer : public QObject
{
    Q_OBJECT
public:
    HttpFileServer(quint16 port, const QString &rootDir,
                   RoomRegistry *registry, QObject *parent = nullptr);

    bool isListening() const;
    quint16 port() const { return m_port; }

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    void handleApi(QTcpSocket *socket, const QByteArray &method, const QString &path);
    void serveFile(QTcpSocket *socket, const QString &urlPath);
    void sendJson(QTcpSocket *socket, int code, const QJsonObject &body);
    void sendResponse(QTcpSocket *socket, int code, const QByteArray &status,
                      const QByteArray &contentType, const QByteArray &body,
                      const QByteArray &cacheControl = QByteArrayLiteral("no-store"));
    static QByteArray statusText(int code);
    static QByteArray mimeFor(const QString &path);

    QTcpServer *m_server;
    RoomRegistry *m_registry;   // не владеет (общий с ConferenceServer)
    QString m_root;
    quint16 m_port;
};
