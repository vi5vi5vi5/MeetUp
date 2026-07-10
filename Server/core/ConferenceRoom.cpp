#include "core/ConferenceRoom.h"
#include "core/ClientSession.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

ConferenceRoom::ConferenceRoom(const QString &code, int ownerId)
    : m_code(code),
      m_ownerId(ownerId),
      m_emptySinceMs(QDateTime::currentMSecsSinceEpoch())
{
}

void ConferenceRoom::addParticipant(ClientSession *session)
{
    if (!m_sessions.contains(session))
        m_sessions.append(session);
    m_emptySinceMs = 0;
}

void ConferenceRoom::removeParticipant(ClientSession *session)
{
    m_sessions.removeAll(session);
    if (m_sessions.isEmpty())
        m_emptySinceMs = QDateTime::currentMSecsSinceEpoch();
}

void ConferenceRoom::broadcastJson(const QJsonObject &obj, ClientSession *except) const
{
    for (ClientSession *s : m_sessions)
        if (s != except)
            s->sendJson(obj);
}

void ConferenceRoom::broadcastBinary(const QByteArray &data, ClientSession *except) const
{
    for (ClientSession *s : m_sessions)
        if (s != except)
            s->sendBinary(data);
}

QJsonArray ConferenceRoom::participantsJson() const
{
    QJsonArray arr;
    for (ClientSession *s : m_sessions) {
        QJsonObject o;
        o["id"] = qint64(s->id());
        o["name"] = s->name();
        o["mic"] = s->micOn();
        o["cam"] = s->camOn();
        arr.append(o);
    }
    return arr;
}

void ConferenceRoom::appendChat(const ChatEntry &entry)
{
    m_history.append(entry);
    if (m_history.size() > kMaxHistory)
        m_history.removeFirst();
}

QJsonArray ConferenceRoom::historyJson() const
{
    QJsonArray arr;
    for (const ChatEntry &e : m_history) {
        QJsonObject o;
        o["sender_id"] = qint64(e.senderId);
        o["sender_name"] = e.senderName;
        o["text"] = e.text;
        o["timestamp_ms"] = e.timestampMs;
        arr.append(o);
    }
    return arr;
}
