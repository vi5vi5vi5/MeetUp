#include "core/ClientSession.h"

#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>

ClientSession::ClientSession(QWebSocket *socket, QObject *parent)
    : QObject(parent), m_socket(socket)
{
    // Сессия становится владельцем сокета: удалится вместе с ней.
    m_socket->setParent(this);

    // Самое крупное легитимное сообщение — чат с картинкой (~600 КБ base64);
    // потолок с запасом отсекает попытки завалить сервер гигантским фреймом.
    m_socket->setMaxAllowedIncomingMessageSize(2 * 1024 * 1024);

    connect(m_socket, &QWebSocket::textMessageReceived,
            this, &ClientSession::onTextMessageReceived);
    connect(m_socket, &QWebSocket::binaryMessageReceived,
            this, &ClientSession::onBinaryMessageReceived);
    connect(m_socket, &QWebSocket::disconnected,
            this, &ClientSession::onSocketDisconnected);
}

ClientSession::~ClientSession() = default;

void ClientSession::sendJson(const QJsonObject &obj)
{
    const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    m_socket->sendTextMessage(QString::fromUtf8(json));
}

void ClientSession::sendBinary(const QByteArray &data)
{
    m_socket->sendBinaryMessage(data);
}

void ClientSession::close()
{
    m_socket->close();
}

void ClientSession::onTextMessageReceived(const QString &text)
{
    emit textReceived(this, text);
}

void ClientSession::onBinaryMessageReceived(const QByteArray &data)
{
    emit binaryReceived(this, data);
}

void ClientSession::onSocketDisconnected()
{
    emit disconnected(this);
}
