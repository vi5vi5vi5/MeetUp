#pragma once

#include <algorithm>

#include <QHash>
#include <QList>

#include "interface/Abstract/IPersonalRooms.h"

// Личные комнаты в памяти: живут до перезапуска сервера. Дополнительный
// хеш — индекс по коду (join, проверка занятости).
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

    QList<PersonalRoom> listByOwner(int ownerId) const override
    {
        // Индекса по владельцу больше нет: комнат у него теперь несколько, а
        // держать ради этого список списков в образцовой реализации незачем —
        // проход по хешу здесь и дешевле, и понятнее. В SQLite для того же
        // есть idx_personal_rooms_owner.
        QList<PersonalRoom> out;
        for (const PersonalRoom &r : m_rooms)
            if (r.ownerId == ownerId)
                out.append(r);
        std::sort(out.begin(), out.end(),
                  [](const PersonalRoom &a, const PersonalRoom &b) { return a.id < b.id; });
        return out;
    }

    bool removeBy(int id) override
    {
        const auto it = m_rooms.constFind(id);
        if (it == m_rooms.constEnd())
            return false;
        m_byCode.remove(it->code);
        m_rooms.remove(id);
        return true;
    }

private:
    QHash<int, PersonalRoom> m_rooms;
    QHash<QString, int> m_byCode;
    int m_nextId = 1;
};
