#include "network/HttpFileServer.h"
#include "network/HttpApi.h"
#include "network/HttpRequest.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include <QHostAddress>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

HttpFileServer::HttpFileServer(quint16 port, const QString &rootDir,
                               HttpApi *api, QObject *parent)
    : QObject(parent),
      m_api(api),
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

HttpFileServer::~HttpFileServer()
{
    qDeleteAll(m_parsers);
    m_parsers.clear();
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
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            delete m_parsers.take(socket);
            socket->deleteLater();
        });
    }
}

void HttpFileServer::onReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    // Запрос может приходить кусками (особенно POST с телом) — парсер копит
    // байты между вызовами readyRead, пока не соберёт запрос целиком.
    HttpRequestParser *parser = m_parsers.value(socket);
    if (!parser) {
        parser = new HttpRequestParser;
        m_parsers.insert(socket, parser);
    }

    switch (parser->feed(socket->readAll())) {
    case HttpRequestParser::State::NeedMore:
        return;
    case HttpRequestParser::State::Error: {
        const int code = parser->errorStatus();
        sendResponse(socket, code, statusText(code), "text/plain; charset=utf-8",
                     QByteArray::number(code) + ' ' + statusText(code));
        break;
    }
    case HttpRequestParser::State::Done:
        dispatch(socket, parser);
        break;
    }

    // Ответ отправлен, соединение закрывается — байты, досланные клиентом
    // до фактического закрытия, не должны диспетчеризоваться повторно.
    delete m_parsers.take(socket);
}

void HttpFileServer::dispatch(QTcpSocket *socket, HttpRequestParser *parser)
{
    const HttpRequest &req = parser->request();

    // Сначала API: HttpApi берёт на себя любой путь внутри /api. Ответ может
    // прийти позже (register/login хешируют пароль в пуле потоков) — к этому
    // моменту клиент мог отвалиться, QPointer это заметит.
    if (m_api) {
        QPointer<QTcpSocket> guard(socket);
        const bool handled = m_api->route(req, [this, guard](const ApiResponse &resp) {
            if (guard)
                sendApiResponse(guard, resp);
        });
        if (handled)
            return;
    }

    if (req.method != "GET") {
        sendResponse(socket, 405, statusText(405), "text/plain; charset=utf-8",
                     "405 Method Not Allowed");
        return;
    }

    serveFile(socket, req.path);
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

void HttpFileServer::sendApiResponse(QTcpSocket *socket, const ApiResponse &resp)
{
    // Бинарный ответ (аватарки): contentType задан — body-JSON не используется.
    if (!resp.contentType.isEmpty()) {
        sendResponse(socket, resp.status, statusText(resp.status), resp.contentType,
                     resp.rawBody,
                     resp.cacheControl.isEmpty() ? QByteArrayLiteral("no-store")
                                                 : resp.cacheControl,
                     resp.headers);
        return;
    }
    sendResponse(socket, resp.status, statusText(resp.status),
                 "application/json; charset=utf-8",
                 QJsonDocument(resp.body).toJson(QJsonDocument::Compact),
                 QByteArrayLiteral("no-store"), resp.headers);
}

void HttpFileServer::sendJson(QTcpSocket *socket, int code, const QJsonObject &body)
{
    sendResponse(socket, code, statusText(code), "application/json; charset=utf-8",
                 QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void HttpFileServer::sendResponse(QTcpSocket *socket, int code, const QByteArray &status,
                                  const QByteArray &contentType, const QByteArray &body,
                                  const QByteArray &cacheControl,
                                  const QList<QPair<QByteArray, QByteArray>> &extraHeaders)
{
    QByteArray header;
    header += "HTTP/1.1 " + QByteArray::number(code) + ' ' + status + "\r\n";
    header += "Content-Type: " + contentType + "\r\n";
    header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    header += "Cache-Control: " + cacheControl + "\r\n";
    for (const auto &kv : extraHeaders)
        header += kv.first + ": " + kv.second + "\r\n";
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
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Content Too Large";
    case 431: return "Request Header Fields Too Large";
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
