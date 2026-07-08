#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QJsonObject;
class HttpApi;
class HttpRequestParser;
struct ApiResponse;

// HTTP-транспорт двух ролей:
//   1) статика веб-клиента (web/): страница по http://localhost открывается
//      без установки Python или отдельного веб-сервера;
//   2) JSON API (/api/...): запросы разбираются здесь (HttpRequestParser),
//      маршрутизацией и вызовом сервисов занимается HttpApi.
// Точкой входа служит login.html — «/» отдаёт именно его.
class HttpFileServer : public QObject
{
    Q_OBJECT
public:
    HttpFileServer(quint16 port, const QString &rootDir,
                   HttpApi *api, QObject *parent = nullptr);
    ~HttpFileServer() override;

    bool isListening() const;
    quint16 port() const { return m_port; }

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    void dispatch(QTcpSocket *socket, HttpRequestParser *parser);
    void serveFile(QTcpSocket *socket, const QString &urlPath);
    void sendApiResponse(QTcpSocket *socket, const ApiResponse &resp);
    void sendJson(QTcpSocket *socket, int code, const QJsonObject &body);
    void sendResponse(QTcpSocket *socket, int code, const QByteArray &status,
                      const QByteArray &contentType, const QByteArray &body,
                      const QByteArray &cacheControl = QByteArrayLiteral("no-store"),
                      const QList<QPair<QByteArray, QByteArray>> &extraHeaders = {});
    static QByteArray statusText(int code);
    static QByteArray mimeFor(const QString &path);

    QTcpServer *m_server;
    HttpApi *m_api;   // не владеет (создаётся в main)
    QHash<QTcpSocket *, HttpRequestParser *> m_parsers;   // парсер на соединение
    QString m_root;
    quint16 m_port;
};
