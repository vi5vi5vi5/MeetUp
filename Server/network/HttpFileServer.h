#pragma once

#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

// Крошечный статический HTTP-сервер: отдаёт файлы веб-клиента (index.html и т.п.),
// чтобы страница открывалась по http://localhost (secure context для getUserMedia)
// без установки Python или отдельного веб-сервера. Только GET, только чтение.
class HttpFileServer : public QObject
{
    Q_OBJECT
public:
    HttpFileServer(quint16 port, const QString &rootDir, QObject *parent = nullptr);

    bool isListening() const;
    quint16 port() const { return m_port; }

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    void serveFile(QTcpSocket *socket, const QString &urlPath);
    void sendResponse(QTcpSocket *socket, int code, const QByteArray &status,
                      const QByteArray &contentType, const QByteArray &body);
    static QByteArray mimeFor(const QString &path);

    QTcpServer *m_server;
    QString m_root;
    quint16 m_port;
};
