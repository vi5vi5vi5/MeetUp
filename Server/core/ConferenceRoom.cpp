#include "core/ConferenceRoom.h"
#include "core/ClientSession.h"

#include <QJsonObject>
#include <QJsonArray>

ConferenceRoom::ConferenceRoom(const QString &code)
    : m_code(code)
{
}

void ConferenceRoom::addParticipant(ClientSession *session)
{
    if (!m_sessions.contains(session))
        m_sessions.append(session);
}

void ConferenceRoom::removeParticipant(ClientSession *session)
{
    m_sessions.removeAll(session);
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
        arr.append(o);
    }
    return arr;
}
