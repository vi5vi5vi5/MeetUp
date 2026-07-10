#pragma once

#include <QHash>

#include "interface/Abstract/IPersonalRooms.h"

// Личные комнаты в памяти: живут до перезапуска сервера. Дополнительные
// хеши — индексы по коду (join, проверка занятости) и владельцу (главная).
class InMemoryPersonalRooms : public IPersonalRooms
{
public:
    void save(PersonalRoom &room) override
    {
        if (room.id == -1)
            room.id = m_nextId++;
        else if (room.id >= m_nextId)
            m_nextId = room.id + 1;

        const auto old = m_rooms.constFind(room.id);
        if (old != m_rooms.constEnd() && old->code != room.code)
            m_byCode.remove(old->code);

        m_rooms.insert(room.id, room);
        m_byCode.insert(room.code, room.id);
        m_byOwner.insert(room.ownerId, room.id);
    }

    std::optional<PersonalRoom> findById(int id) const override
    {
        const auto it = m_rooms.constFind(id);
        if (it == m_rooms.constEnd())
            return std::nullopt;
        return *it;
    }

    std::optional<PersonalRoom> findByCode(const QString &code) const override
    {
        const auto it = m_byCode.constFind(code);
        if (it == m_byCode.constEnd())
            return std::nullopt;
        return findById(*it);
    }

    std::optional<PersonalRoom> findByOwner(int ownerId) const override
    {
        const auto it = m_byOwner.constFind(ownerId);
        if (it == m_byOwner.constEnd())
            return std::nullopt;
        return findById(*it);
    }

    bool removeBy(int id) override
    {
        const auto it = m_rooms.constFind(id);
        if (it == m_rooms.constEnd())
            return false;
        m_byCode.remove(it->code);
        m_byOwner.remove(it->ownerId);
        m_rooms.remove(id);
        return true;
    }

private:
    QHash<int, PersonalRoom> m_rooms;
    QHash<QString, int> m_byCode;
    QHash<int, int> m_byOwner;
    int m_nextId = 1;
};
