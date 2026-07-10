#include "network/ConferenceServer.h"
#include "core/ClientSession.h"
#include "core/ConferenceRoom.h"
#include "core/RoomRegistry.h"
#include "network/HttpRequest.h"
#include "services/AuthService.h"
#include "services/PersonalRoomService.h"

#include <QWebSocketServer>
#include <QWebSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QTimer>
#include <QDebug>

namespace {
// Имя куки сессии — то же, что ставит HTTP API (см. network/HttpApi.cpp).
const QByteArray kSessionCookieName = QByteArrayLiteral("meetup_session");
} // namespace

ConferenceServer::ConferenceServer(quint16 port, RoomRegistry *registry,
                                   std::shared_ptr<AuthService> auth,
                                   std::shared_ptr<PersonalRoomService> personalRooms,
                                   QObject *parent)
    : QObject(parent), m_registry(registry), m_auth(std::move(auth)),
      m_personalRooms(std::move(personalRooms)), m_port(port)
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

    auto *purgeTimer = new QTimer(this);
    connect(purgeTimer, &QTimer::timeout, this, &ConferenceServer::purgeIdleRooms);
    purgeTimer->start(kPurgeIntervalMs);
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

        // Живая сессия аккаунта в куке хендшейка — запоминаем, кто это:
        // join получит постоянный id и имя из профиля.
        const QString token = QString::fromLatin1(HttpRequest::cookieValue(
            socket->request().rawHeader(QByteArrayLiteral("Cookie")), kSessionCookieName));
        if (!token.isEmpty()) {
            if (const std::optional<User> user = m_auth->userByToken(token))
                session->setAccount(user->id, user->displayName);
        }

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
    else if (type == QLatin1String("state"))
        handleState(session, msg);
    else
        sendError(session, QStringLiteral("unknown_type"));
}

void ConferenceServer::handleJoin(ClientSession *session, const QJsonObject &msg)
{
    const QString roomCode = msg.value(QStringLiteral("room")).toString().trimmed();
    const bool account = session->userId() >= 0;

    // Имя авторизованного берётся из профиля: оно авторитетнее присланного,
    // и клиенту не нужно спрашивать его на гейте.
    const QString name = account
        ? session->accountName()
        : msg.value(QStringLiteral("name")).toString().trimmed().left(64);

    if (roomCode.isEmpty() || name.isEmpty()) {
        sendError(session, QStringLiteral("invalid_join"));
        return;
    }
    if (session->room()) {
        sendError(session, QStringLiteral("already_joined"));
        return;
    }

    // Комнату создаёт HTTP API — опечатка в коде не плодит комнат-призраков.
    // Личная комната — исключение: её открывает join владельца.
    ConferenceRoom *room = m_registry->find(roomCode);
    if (!room) {
        const std::optional<PersonalRoom> personal = m_personalRooms->byCode(roomCode);
        if (personal.has_value())
            room = m_registry->find(personal->code);   // код мог отличаться регистром
        if (!room) {
            if (!personal.has_value()) {
                sendError(session, QStringLiteral("room_not_found"));
                return;
            }
            if (!account || session->userId() != personal->ownerId) {
                // Личная комната без владельца не существует: гости ждут,
                // пока владелец её откроет.
                sendError(session, QStringLiteral("room_offline"));
                return;
            }
            room = m_registry->createPersonal(personal->code, personal->ownerId);
        }
    }

    // Правила личной комнаты проверяются по свежим данным сервиса: пароль
    // и код могли поменяться, пока комната в эфире.
    QString roomTitle;
    if (room->ownerId() >= 0) {
        const std::optional<PersonalRoom> personal = m_personalRooms->byCode(room->code());
        if (!personal.has_value() || personal->ownerId != room->ownerId()) {
            // Комнату удалили или сменили ей код: по старому коду больше
            // никого не пускаем, участники внутри остаются.
            sendError(session, QStringLiteral("room_not_found"));
            return;
        }
        const bool isOwner = account && session->userId() == personal->ownerId;
        if (!isOwner) {
            // Активной комнату делает вход владельца; дальше она живёт, пока
            // в ней кто-то есть — гости входят и без владельца. Опустевшую
            // заново открывает только владелец.
            if (room->isEmpty()) {
                sendError(session, QStringLiteral("room_offline"));
                return;
            }
            if (!personal->password.isEmpty()
                && msg.value(QStringLiteral("password")).toString() != personal->password) {
                sendError(session, QStringLiteral("wrong_password"));
                return;
            }
        }
        roomTitle = personal->title;
    }

    if (account) {
        // Постоянный id: тот же пользователь — тот же id в любой конференции.
        session->setId(kAccountIdBase + quint32(session->userId()));
        dropDuplicate(room, session->id());
    } else {
        session->setId(m_nextId++);
    }
    session->setName(name);

    // join_ok — только вошедшему: список присутствующих и история чата,
    // чтобы новый участник видел разговор, который шёл до него.
    QJsonObject joinOk{
        {"type", "join_ok"},
        {"sender_id", qint64(session->id())},
        {"participants", room->participantsJson()},
        {"history", room->historyJson()},
    };
    if (!roomTitle.isEmpty())
        joinOk.insert(QStringLiteral("room_title"), roomTitle);
    session->sendJson(joinOk);

    // Теперь добавляем в комнату и уведомляем остальных.
    room->addParticipant(session);
    session->setRoom(room);

    room->broadcastJson(QJsonObject{
        {"type", "participant_joined"},
        {"id", qint64(session->id())},
        {"name", session->name()},
        {"mic", session->micOn()},
        {"cam", session->camOn()},
    }, session);

    qInfo().noquote() << QStringLiteral("join: %1 (id=%2) -> room '%3', participants: %4")
                             .arg(name).arg(session->id()).arg(roomCode)
                             .arg(room->sessions().size());
}

void ConferenceServer::dropDuplicate(ConferenceRoom *room, quint32 id)
{
    ClientSession *dup = nullptr;
    for (ClientSession *s : room->sessions()) {
        if (s->id() == id) {
            dup = s;
            break;
        }
    }
    if (!dup)
        return;

    // Старое соединение вытесняется новым: session_replaced говорит клиенту
    // не переподключаться (иначе две вкладки выбивали бы друг друга вечно).
    sendError(dup, QStringLiteral("session_replaced"));
    room->removeParticipant(dup);
    dup->setRoom(nullptr);
    dup->close();
    room->broadcastJson(QJsonObject{
        {"type", "participant_left"},
        {"id", qint64(id)},
    });

    qInfo().noquote() << QStringLiteral("join: id=%1 replaced by a new connection in room '%2'")
                             .arg(id).arg(room->code());
}

void ConferenceServer::handleChat(ClientSession *session, const QJsonObject &msg)
{
    ConferenceRoom *room = session->room();
    if (!room) {
        sendError(session, QStringLiteral("not_in_room"));
        return;
    }

    const QString text = msg.value(QStringLiteral("text")).toString().left(2000);
    if (text.isEmpty())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    room->appendChat(ChatEntry{session->id(), session->name(), text, now});

    // Рассылаем всем, включая отправителя — единая лента у всех. Имя дублируем
    // в сообщении: получатель мог зайти позже отправителя и не знать его id.
    room->broadcastJson(QJsonObject{
        {"type", "chat"},
        {"sender_id", qint64(session->id())},
        {"sender_name", session->name()},
        {"text", text},
        {"timestamp_ms", now},
    });
}

void ConferenceServer::handleState(ClientSession *session, const QJsonObject &msg)
{
    ConferenceRoom *room = session->room();
    if (!room) {
        sendError(session, QStringLiteral("not_in_room"));
        return;
    }

    if (msg.contains(QStringLiteral("mic")))
        session->setMicOn(msg.value(QStringLiteral("mic")).toBool(session->micOn()));
    if (msg.contains(QStringLiteral("cam")))
        session->setCamOn(msg.value(QStringLiteral("cam")).toBool(session->camOn()));

    room->broadcastJson(QJsonObject{
        {"type", "participant_state"},
        {"id", qint64(session->id())},
        {"mic", session->micOn()},
        {"cam", session->camOn()},
    }, session);
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
        // Опустевшую комнату не удаляем сразу: participants могли отвалиться
        // по сети и вернуться. Брошенные комнаты собирает purgeIdleRooms().
    }

    m_sessions.remove(session);
    session->deleteLater();

    qInfo() << "Client disconnected, active sessions:" << m_sessions.size();
}

void ConferenceServer::purgeIdleRooms()
{
    const int removed = m_registry->purgeIdle(kRoomIdleTtlMs);
    if (removed > 0)
        qInfo() << "Purged idle rooms:" << removed << "left:" << m_registry->roomCount();
}

void ConferenceServer::sendError(ClientSession *session, const QString &reason)
{
    session->sendJson(QJsonObject{
        {"type", "error"},
        {"reason", reason},
    });
}
