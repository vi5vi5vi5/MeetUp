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
    if (m_sessions.isEmpty())
        m_liveSinceMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_sessions.contains(session))
        m_sessions.append(session);
    m_emptySinceMs = 0;
}

void ConferenceRoom::removeParticipant(ClientSession *session)
{
    if (session == m_screenSharer)
        m_screenSharer = nullptr;
    m_sessions.removeAll(session);
    if (m_sessions.isEmpty()) {
        m_emptySinceMs = QDateTime::currentMSecsSinceEpoch();
        m_liveSinceMs = 0;
    }
}

void ConferenceRoom::broadcastJson(const QJsonObject &obj, ClientSession *except) const
{
    for (ClientSession *s : m_sessions)
        if (s != except)
            s->sendJson(obj);
}

// Ретрансляция медиакадра.
//
// dropIfBusy решает судьбу получателя, чей канал уже́ потока. Раньше выбора не
// было: сервер отдавал кадр всем подряд, и у отстающего очередь росла без
// границы — прямо в памяти сервера. Ронять её было некому, поэтому кадры
// доезжали все, но со всё большим опозданием: через минуту такой демонстрации
// человек смотрел прошлое и не мог понять, почему «интернет вроде есть».
//
// Ронять видеокадры при этом безопасно ровно потому, что приёмник умеет
// попросить опорный (KEYFRAME_REQ): пропуск лечится сам и стоит секунды
// заглушки вместо минуты растущего отставания. А вот звук и служебные
// сообщения не роняем никогда — они мелкие, и терять их незачем.
void ConferenceRoom::broadcastBinary(const QByteArray &data, ClientSession *except,
                                     bool dropIfBusy) const
{
    for (ClientSession *s : m_sessions) {
        if (s == except)
            continue;
        if (dropIfBusy && s->pendingBytes() > kSlowClientBytes)
            continue;
        s->sendBinary(data);
    }
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
        if (s->userId() >= 0) {
            // Аватарка авторизованного: клиент строит URL по user_id и версии.
            o["user_id"] = s->userId();
            if (s->avatarVer() > 0)
                o["avatar"] = s->avatarVer();
        }
        arr.append(o);
    }
    return arr;
}

void ConferenceRoom::appendChat(const ChatEntry &entry)
{
    m_history.append(entry);
    if (m_history.size() > kMaxHistory)
        m_history.removeFirst();

    if (entry.image.isEmpty())
        return;

    // Держим только kMaxHistoryImages свежих картинок: у более старых
    // освобождаем данные, оставляя пометку — клиент покажет заглушку.
    int images = 0;
    for (int i = m_history.size() - 1; i >= 0; --i) {
        ChatEntry &e = m_history[i];
        if (e.image.isEmpty())
            continue;
        if (++images > kMaxHistoryImages) {
            e.image.clear();
            e.imageDropped = true;
        }
    }
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
        if (!e.image.isEmpty())
            o["image"] = e.image;
        if (e.imageDropped)
            o["image_dropped"] = true;
        arr.append(o);
    }
    return arr;
}
