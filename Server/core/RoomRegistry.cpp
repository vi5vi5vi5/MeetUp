#include "core/RoomRegistry.h"
#include "core/ConferenceRoom.h"

RoomRegistry::~RoomRegistry()
{
    qDeleteAll(m_rooms);
    m_rooms.clear();
}

ConferenceRoom *RoomRegistry::getOrCreate(const QString &code)
{
    auto it = m_rooms.find(code);
    if (it != m_rooms.end())
        return it.value();

    auto *room = new ConferenceRoom(code);
    m_rooms.insert(code, room);
    return room;
}

ConferenceRoom *RoomRegistry::find(const QString &code) const
{
    return m_rooms.value(code, nullptr);
}

void RoomRegistry::removeIfEmpty(ConferenceRoom *room)
{
    if (room && room->isEmpty()) {
        m_rooms.remove(room->code());
        delete room;
    }
}
