#include "network/HttpFileServer.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QDebug>

HttpFileServer::HttpFileServer(quint16 port, const QString &rootDir, QObject *parent)
    : QObject(parent),
      m_root(QDir(rootDir).absolutePath()),
      m_port(port)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this, &HttpFileServer::onNewConnection);

    if (m_server->listen(QHostAddress::Any, port))
        qInfo().noquote() << QStringLiteral("HTTP serving %1 at http://localhost:%2")
                                 .arg(m_root).arg(port);
    else
        qCritical().noquote() << QStringLiteral("HTTP: failed to listen on port %1: %2")
                                     .arg(port).arg(m_server->errorString());
}

bool HttpFileServer::isListening() const
{
    return m_server->isListening();
}

void HttpFileServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &HttpFileServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void HttpFileServer::onReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    // GET-запрос без тела помещается в один-два пакета; для тестового стенда
    // достаточно разобрать первую строку запроса: "GET /path HTTP/1.1".
    const QByteArray request = socket->readAll();
    const int lineEnd = request.indexOf('\n');
    const QByteArray requestLine = (lineEnd >= 0 ? request.left(lineEnd) : request).trimmed();

    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        socket->disconnectFromHost();
        return;
    }

    QString path = QString::fromUtf8(parts.at(1));
    const int query = path.indexOf('?');
    if (query >= 0)
        path = path.left(query);

    serveFile(socket, path);
}

void HttpFileServer::serveFile(QTcpSocket *socket, const QString &urlPath)
{
    QString rel = QUrl::fromPercentEncoding(urlPath.toUtf8());
    if (rel.isEmpty() || rel == QLatin1String("/"))
        rel = QStringLiteral("/index.html");

    // Нормализуем путь и не выпускаем за пределы корневой папки.
    const QString full = QDir::cleanPath(m_root + rel);
    if (!full.startsWith(m_root)) {
        sendResponse(socket, 403, "Forbidden", "text/plain; charset=utf-8", "403 Forbidden");
        return;
    }

    QFile file(full);
    if (!QFileInfo(full).isFile() || !file.open(QIODevice::ReadOnly)) {
        sendResponse(socket, 404, "Not Found", "text/plain; charset=utf-8", "404 Not Found");
        return;
    }

    sendResponse(socket, 200, "OK", mimeFor(full), file.readAll());
}

void HttpFileServer::sendResponse(QTcpSocket *socket, int code, const QByteArray &status,
                                  const QByteArray &contentType, const QByteArray &body)
{
    QByteArray header;
    header += "HTTP/1.1 " + QByteArray::number(code) + ' ' + status + "\r\n";
    header += "Content-Type: " + contentType + "\r\n";
    header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    header += "Cache-Control: no-store\r\n";
    header += "Connection: close\r\n\r\n";

    socket->write(header);
    socket->write(body);
    socket->flush();
    socket->disconnectFromHost();
}

QByteArray HttpFileServer::mimeFor(const QString &path)
{
    if (path.endsWith(QLatin1String(".html"))) return "text/html; charset=utf-8";
    if (path.endsWith(QLatin1String(".js")))   return "application/javascript; charset=utf-8";
    if (path.endsWith(QLatin1String(".css")))  return "text/css; charset=utf-8";
    if (path.endsWith(QLatin1String(".json"))) return "application/json; charset=utf-8";
    if (path.endsWith(QLatin1String(".png")))  return "image/png";
    if (path.endsWith(QLatin1String(".jpg")) || path.endsWith(QLatin1String(".jpeg")))
        return "image/jpeg";
    if (path.endsWith(QLatin1String(".svg")))  return "image/svg+xml";
    if (path.endsWith(QLatin1String(".ico")))  return "image/x-icon";
    return "application/octet-stream";
}
