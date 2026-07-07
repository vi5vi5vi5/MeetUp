#include "network/ConferenceServer.h"
#include "core/ClientSession.h"
#include "core/ConferenceRoom.h"

#include <QWebSocketServer>
#include <QWebSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>

ConferenceServer::ConferenceServer(quint16 port, QObject *parent)
    : QObject(parent), m_port(port)
{
    m_server = new QWebSocketServer(QStringLiteral("MeetUp"),
                                    QWebSocketServer::NonSecureMode, this);

    if (m_server->listen(QHostAddress::Any, port)) {
        connect(m_server, &QWebSocketServer::newConnection,
                this, &ConferenceServer::onNewConnection);
        qInfo().noquote() << QStringLiteral("WebSocket relay listening on ws://localhost:%1").arg(port);
    } else {
        qCritical().noquote() << QStringLiteral("Failed to listen on port %1: %2")
                                     .arg(port).arg(m_server->errorString());
    }
}

ConferenceServer::~ConferenceServer()
{
    m_server->close();
}

bool ConferenceServer::isListening() const
{
    return m_server->isListening();
}

void ConferenceServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QWebSocket *socket = m_server->nextPendingConnection();
        auto *session = new ClientSession(socket, this);
        m_sessions.insert(session);

        connect(session, &ClientSession::textReceived,
                this, &ConferenceServer::onText);
        connect(session, &ClientSession::binaryReceived,
                this, &ConferenceServer::onBinary);
        connect(session, &ClientSession::disconnected,
                this, &ConferenceServer::onDisconnected);

        qInfo() << "New connection, active sessions:" << m_sessions.size();
    }
}

void ConferenceServer::onText(ClientSession *session, const QString &text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject()) {
        sendError(session, QStringLiteral("invalid_json"));
        return;
    }

    const QJsonObject msg = doc.object();
    const QString type = msg.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("join"))
        handleJoin(session, msg);
    else if (type == QLatin1String("chat"))
        handleChat(session, msg);
    else
        sendError(session, QStringLiteral("unknown_type"));
}

void ConferenceServer::handleJoin(ClientSession *session, const QJsonObject &msg)
{
    const QString roomCode = msg.value(QStringLiteral("room")).toString().trimmed();
    const QString name = msg.value(QStringLiteral("name")).toString().trimmed();

    if (roomCode.isEmpty() || name.isEmpty()) {
        sendError(session, QStringLiteral("invalid_join"));
        return;
    }
    if (session->room()) {
        sendError(session, QStringLiteral("already_joined"));
        return;
    }

    ConferenceRoom *room = m_registry.getOrCreate(roomCode);
    session->setId(m_nextId++);
    session->setName(name);

    // join_ok — только вошедшему, со списком уже присутствующих участников.
    session->sendJson(QJsonObject{
        {"type", "join_ok"},
        {"sender_id", qint64(session->id())},
        {"participants", room->participantsJson()},
    });

    // Теперь добавляем в комнату и уведомляем остальных.
    room->addParticipant(session);
    session->setRoom(room);

    room->broadcastJson(QJsonObject{
        {"type", "participant_joined"},
        {"id", qint64(session->id())},
        {"name", session->name()},
    }, session);

    qInfo().noquote() << QStringLiteral("join: %1 (id=%2) -> room '%3', participants: %4")
                             .arg(name).arg(session->id()).arg(roomCode)
                             .arg(room->sessions().size());
}

void ConferenceServer::handleChat(ClientSession *session, const QJsonObject &msg)
{
    ConferenceRoom *room = session->room();
    if (!room) {
        sendError(session, QStringLiteral("not_in_room"));
        return;
    }

    const QString text = msg.value(QStringLiteral("text")).toString();
    if (text.isEmpty())
        return;

    // Рассылаем всем, включая отправителя — единая лента у всех.
    room->broadcastJson(QJsonObject{
        {"type", "chat"},
        {"sender_id", qint64(session->id())},
        {"text", text},
        {"timestamp_ms", qint64(QDateTime::currentMSecsSinceEpoch())},
    });
}

void ConferenceServer::onBinary(ClientSession *session, const QByteArray &data)
{
    ConferenceRoom *room = session->room();
    if (!room)
        return;

    // Заголовок клиента: [type:1][timestamp_ms:8]. Минимум 9 байт.
    if (data.size() < 9)
        return;

    // Сервер вставляет sender_id (uint32 LE) сразу после message_type,
    // получая заголовок 13 байт: [type:1][sender_id:4][timestamp_ms:8].
    const quint32 id = session->id();
    QByteArray out;
    out.reserve(data.size() + 4);
    out.append(data.at(0));                 // message_type
    out.append(char(id & 0xFF));
    out.append(char((id >> 8) & 0xFF));
    out.append(char((id >> 16) & 0xFF));
    out.append(char((id >> 24) & 0xFF));
    out.append(data.mid(1));                // timestamp_ms(8) + payload

    room->broadcastBinary(out, session);
}

void ConferenceServer::onDisconnected(ClientSession *session)
{
    ConferenceRoom *room = session->room();
    if (room) {
        room->removeParticipant(session);
        room->broadcastJson(QJsonObject{
            {"type", "participant_left"},
            {"id", qint64(session->id())},
        });
        m_registry.removeIfEmpty(room);
    }

    m_sessions.remove(session);
    session->deleteLater();

    qInfo() << "Client disconnected, active sessions:" << m_sessions.size();
}

void ConferenceServer::sendError(ClientSession *session, const QString &reason)
{
    session->sendJson(QJsonObject{
        {"type", "error"},
        {"reason", reason},
    });
}
