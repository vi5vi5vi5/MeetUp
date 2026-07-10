#include "core/RoomRegistry.h"
#include "core/ConferenceRoom.h"

#include <QDateTime>
#include <QRandomGenerator>

RoomRegistry::~RoomRegistry()
{
    qDeleteAll(m_rooms);
    m_rooms.clear();
}

QString RoomRegistry::generateCode()
{
    // 24 символа [A-Za-z0-9] ≈ 143 бита энтропии: код нельзя подобрать,
    // поэтому ссылка-приглашение и есть «секрет» комнаты.
    // QRandomGenerator::system() — криптостойкий источник ОС.
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    constexpr int alphabetSize = int(sizeof(alphabet)) - 1;
    constexpr int codeLength = 24;

    QString code;
    code.reserve(codeLength);
    for (int i = 0; i < codeLength; ++i)
        code.append(QLatin1Char(alphabet[QRandomGenerator::system()->bounded(alphabetSize)]));
    return code;
}

ConferenceRoom *RoomRegistry::createRoom()
{
    QString code = generateCode();
    while (m_rooms.contains(code))   // коллизия практически невозможна
        code = generateCode();

    auto *room = new ConferenceRoom(code);
    m_rooms.insert(code, room);
    return room;
}

ConferenceRoom *RoomRegistry::createPersonal(const QString &code, int ownerId)
{
    if (m_rooms.contains(code))
        return nullptr;

    auto *room = new ConferenceRoom(code, ownerId);
    m_rooms.insert(code, room);
    return room;
}

ConferenceRoom *RoomRegistry::find(const QString &code) const
{
    return m_rooms.value(code, nullptr);
}

int RoomRegistry::purgeIdle(qint64 ttlMs)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int removed = 0;
    for (auto it = m_rooms.begin(); it != m_rooms.end(); ) {
        ConferenceRoom *room = it.value();
        if (room->isEmpty() && now - room->emptySinceMs() > ttlMs) {
            delete room;
            it = m_rooms.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}
