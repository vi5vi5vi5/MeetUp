#pragma once

#include <QObject>
#include <QSet>

#include <memory>

class QWebSocketServer;
class QJsonObject;
class AuthService;
class ClientSession;
class ConferenceRoom;
class PersonalRoomService;
class RoomRegistry;

// Ядро сервера. Принимает WebSocket-соединения, заворачивает каждое в
// ClientSession, разбирает входящие сообщения и маршрутизирует их по комнатам:
//   - текст (JSON): join / chat / state;
//   - бинарь: медиа-кадры, релеятся остальным участникам комнаты.
// Комнаты создаёт HTTP API (POST /api/rooms) — join возможен только в
// существующую. Исключение — личная комната: её открывает join владельца.
// Реестром владеет main, он общий с HttpFileServer.
//
// Кука meetup_session в WS-хендшейке привязывает соединение к аккаунту:
// авторизованный участник получает постоянный id (kAccountIdBase + user id)
// и имя из профиля, аноним — очередной id со счётчика.
class ConferenceServer : public QObject
{
    Q_OBJECT
public:
    ConferenceServer(quint16 port, RoomRegistry *registry,
                     std::shared_ptr<AuthService> auth,
                     std::shared_ptr<PersonalRoomService> personalRooms,
                     QObject *parent = nullptr);
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

    // Выгнать из комнаты участника с тем же id (повторный вход с аккаунта:
    // другая вкладка или реконнект, пока старый сокет ещё не умер).
    void dropDuplicate(ConferenceRoom *room, quint32 id);

    // Есть ли в комнате участник с этим аккаунтом (личная комната пускает
    // гостей только при владельце внутри).
    static bool hasAccountUser(const ConferenceRoom *room, int userId);

    QWebSocketServer *m_server;
    RoomRegistry *m_registry;           // не владеет (общий с HTTP API)
    std::shared_ptr<AuthService> m_auth;
    std::shared_ptr<PersonalRoomService> m_personalRooms;
    QSet<ClientSession *> m_sessions;   // все живые сессии (сервер — владелец)
    quint16 m_port;
    quint32 m_nextId = 1;               // раздаётся анонимам при join

    // id авторизованных живут в верхней половине диапазона quint32 —
    // счётчик анонимов туда не дотянется, коллизий нет.
    static constexpr quint32 kAccountIdBase = 0x80000000;

    // Пустые комнаты живут ещё 10 минут: обрыв связи последнего участника
    // или пауза между созданием комнаты и первым join не убивают её.
    static constexpr qint64 kRoomIdleTtlMs = 10 * 60 * 1000;
    static constexpr int kPurgeIntervalMs = 60 * 1000;
};
