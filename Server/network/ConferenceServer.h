#pragma once

#include <QObject>
#include <QSet>

class QWebSocketServer;
class QJsonObject;
class ClientSession;
class RoomRegistry;

// Ядро сервера. Принимает WebSocket-соединения, заворачивает каждое в
// ClientSession, разбирает входящие сообщения и маршрутизирует их по комнатам:
//   - текст (JSON): join / chat / state;
//   - бинарь: медиа-кадры, релеятся остальным участникам комнаты.
// Комнаты создаёт HTTP API (POST /api/rooms) — join возможен только в
// существующую. Реестром владеет main, он общий с HttpFileServer.
class ConferenceServer : public QObject
{
    Q_OBJECT
public:
    explicit ConferenceServer(quint16 port, RoomRegistry *registry, QObject *parent = nullptr);
    ~ConferenceServer() override;

    bool isListening() const;
    quint16 port() const { return m_port; }

private slots:
    void onNewConnection();
    void onText(ClientSession *session, const QString &text);
    void onBinary(ClientSession *session, const QByteArray &data);
    void onDisconnected(ClientSession *session);
    void purgeIdleRooms();

private:
    void handleJoin(ClientSession *session, const QJsonObject &msg);
    void handleChat(ClientSession *session, const QJsonObject &msg);
    void handleState(ClientSession *session, const QJsonObject &msg);
    void sendError(ClientSession *session, const QString &reason);

    QWebSocketServer *m_server;
    RoomRegistry *m_registry;           // не владеет (общий с HTTP API)
    QSet<ClientSession *> m_sessions;   // все живые сессии (сервер — владелец)
    quint16 m_port;
    quint32 m_nextId = 1;               // раздаётся клиентам при join

    // Пустые комнаты живут ещё 10 минут: обрыв связи последнего участника
    // или пауза между созданием комнаты и первым join не убивают её.
    static constexpr qint64 kRoomIdleTtlMs = 10 * 60 * 1000;
    static constexpr int kPurgeIntervalMs = 60 * 1000;
};
