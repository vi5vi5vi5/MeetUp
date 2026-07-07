#pragma once

#include <QObject>
#include <QSet>

#include "core/RoomRegistry.h"

class QWebSocketServer;
class QJsonObject;
class ClientSession;

// Ядро сервера. Принимает WebSocket-соединения, заворачивает каждое в
// ClientSession, разбирает входящие сообщения и маршрутизирует их по комнатам:
//   - текст (JSON): join / chat;
//   - бинарь: медиа-кадры, релеятся остальным участникам комнаты.
class ConferenceServer : public QObject
{
    Q_OBJECT
public:
    explicit ConferenceServer(quint16 port, QObject *parent = nullptr);
    ~ConferenceServer() override;

    bool isListening() const;
    quint16 port() const { return m_port; }

private slots:
    void onNewConnection();
    void onText(ClientSession *session, const QString &text);
    void onBinary(ClientSession *session, const QByteArray &data);
    void onDisconnected(ClientSession *session);

private:
    void handleJoin(ClientSession *session, const QJsonObject &msg);
    void handleChat(ClientSession *session, const QJsonObject &msg);
    void sendError(ClientSession *session, const QString &reason);

    QWebSocketServer *m_server;
    RoomRegistry m_registry;
    QSet<ClientSession *> m_sessions;   // все живые сессии (сервер — владелец)
    quint16 m_port;
    quint32 m_nextId = 1;               // раздаётся клиентам при join
};
