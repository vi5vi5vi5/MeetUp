#include "network/HttpFileServer.h"
#include "core/ConferenceRoom.h"
#include "core/RoomRegistry.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

HttpFileServer::HttpFileServer(quint16 port, const QString &rootDir,
                               RoomRegistry *registry, QObject *parent)
    : QObject(parent),
      m_registry(registry),
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

    // Запрос помещается в один-два пакета; для этого стенда достаточно
    // разобрать первую строку: "GET /path HTTP/1.1". Тело запроса API
    // не читаем — создание комнаты и заглушки авторизации без параметров.
    const QByteArray request = socket->readAll();
    const int lineEnd = request.indexOf('\n');
    const QByteArray requestLine = (lineEnd >= 0 ? request.left(lineEnd) : request).trimmed();

    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        socket->disconnectFromHost();
        return;
    }

    const QByteArray method = parts.at(0);
    QString path = QString::fromUtf8(parts.at(1));
    const int query = path.indexOf('?');
    if (query >= 0)
        path = path.left(query);
    path = QUrl::fromPercentEncoding(path.toUtf8());

    if (path == QLatin1String("/api") || path.startsWith(QLatin1String("/api/"))) {
        handleApi(socket, method, path);
        return;
    }

    if (method != "GET") {
        sendResponse(socket, 405, statusText(405), "text/plain; charset=utf-8", "405 Method Not Allowed");
        return;
    }

    serveFile(socket, path);
}

void HttpFileServer::handleApi(QTcpSocket *socket, const QByteArray &method, const QString &path)
{
    // POST /api/rooms — новая комната; код генерирует сервер (см. RoomRegistry).
    if (path == QLatin1String("/api/rooms")) {
        if (method != "POST") {
            sendJson(socket, 405, QJsonObject{{"error", "method_not_allowed"}});
            return;
        }
        ConferenceRoom *room = m_registry->createRoom();
        qInfo().noquote() << QStringLiteral("API: room created '%1' (total %2)")
                                 .arg(room->code()).arg(m_registry->roomCount());
        sendJson(socket, 200, QJsonObject{{"room", room->code()}});
        return;
    }

    // GET /api/rooms/<code> — существует ли комната (проверка кода в лобби).
    if (path.startsWith(QLatin1String("/api/rooms/"))) {
        if (method != "GET") {
            sendJson(socket, 405, QJsonObject{{"error", "method_not_allowed"}});
            return;
        }
        const QString code = path.mid(int(qstrlen("/api/rooms/")));
        ConferenceRoom *room = m_registry->find(code);
        if (!room) {
            sendJson(socket, 404, QJsonObject{{"error", "room_not_found"}});
            return;
        }
        sendJson(socket, 200, QJsonObject{
            {"room", room->code()},
            {"participants", int(room->sessions().size())},
        });
        return;
    }

    // Заглушки входа по аккаунту: маршруты зарезервированы, логики нет.
    // TODO(auth): реальные вход/регистрация появятся вместе с хранилищем
    // пользователей (БД); тогда здесь будут проверка пароля и выдача сессии.
    if (path == QLatin1String("/api/auth/login") || path == QLatin1String("/api/auth/register")) {
        sendJson(socket, 501, QJsonObject{{"error", "not_implemented"}});
        return;
    }

    sendJson(socket, 404, QJsonObject{{"error", "unknown_endpoint"}});
}

void HttpFileServer::serveFile(QTcpSocket *socket, const QString &urlPath)
{
    QString rel = urlPath;
    // Точка входа продукта — страница логина (анонимный вход тоже там).
    if (rel.isEmpty() || rel == QLatin1String("/"))
        rel = QStringLiteral("/login.html");

    // Нормализуем путь и не выпускаем за пределы корневой папки.
    const QString full = QDir::cleanPath(m_root + rel);
    if (!full.startsWith(m_root)) {
        sendResponse(socket, 403, statusText(403), "text/plain; charset=utf-8", "403 Forbidden");
        return;
    }

    QFile file(full);
    if (!QFileInfo(full).isFile() || !file.open(QIODevice::ReadOnly)) {
        sendResponse(socket, 404, statusText(404), "text/plain; charset=utf-8", "404 Not Found");
        return;
    }

    // Библиотеки и шрифты в /assets/ неизменны — пусть браузер кэширует;
    // страницы отдаём всегда свежими.
    const QByteArray cache = rel.startsWith(QLatin1String("/assets/"))
        ? QByteArrayLiteral("public, max-age=86400")
        : QByteArrayLiteral("no-store");

    sendResponse(socket, 200, statusText(200), mimeFor(full), file.readAll(), cache);
}

void HttpFileServer::sendJson(QTcpSocket *socket, int code, const QJsonObject &body)
{
    sendResponse(socket, code, statusText(code), "application/json; charset=utf-8",
                 QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void HttpFileServer::sendResponse(QTcpSocket *socket, int code, const QByteArray &status,
                                  const QByteArray &contentType, const QByteArray &body,
                                  const QByteArray &cacheControl)
{
    QByteArray header;
    header += "HTTP/1.1 " + QByteArray::number(code) + ' ' + status + "\r\n";
    header += "Content-Type: " + contentType + "\r\n";
    header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    header += "Cache-Control: " + cacheControl + "\r\n";
    header += "Connection: close\r\n\r\n";

    socket->write(header);
    socket->write(body);
    socket->flush();
    socket->disconnectFromHost();
}

QByteArray HttpFileServer::statusText(int code)
{
    switch (code) {
    case 200: return "OK";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 501: return "Not Implemented";
    default:  return "Unknown";
    }
}

QByteArray HttpFileServer::mimeFor(const QString &path)
{
    if (path.endsWith(QLatin1String(".html")))  return "text/html; charset=utf-8";
    if (path.endsWith(QLatin1String(".js")))    return "application/javascript; charset=utf-8";
    if (path.endsWith(QLatin1String(".css")))   return "text/css; charset=utf-8";
    if (path.endsWith(QLatin1String(".json")))  return "application/json; charset=utf-8";
    if (path.endsWith(QLatin1String(".woff2"))) return "font/woff2";
    if (path.endsWith(QLatin1String(".png")))   return "image/png";
    if (path.endsWith(QLatin1String(".jpg")) || path.endsWith(QLatin1String(".jpeg")))
        return "image/jpeg";
    if (path.endsWith(QLatin1String(".svg")))   return "image/svg+xml";
    if (path.endsWith(QLatin1String(".ico")))   return "image/x-icon";
    return "application/octet-stream";
}
