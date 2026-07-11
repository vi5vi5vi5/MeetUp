#pragma once

#include <QHash>

#include "interface/Abstract/IRoomAliases.h"

// Alias-ссылки в памяти: живут до перезапуска сервера. В отличие от SQLite
// каскада при удалении комнаты нет — но InMemory-слой живёт только в тестах.
class InMemoryRoomAliases : public IRoomAliases
{
public:
    void save(RoomAlias &alias) override
    {
        if (alias.id == -1)
            alias.id = m_nextId++;
        else if (alias.id >= m_nextId)
            m_nextId = alias.id + 1;

        const auto old = m_aliases.constFind(alias.id);
        if (old != m_aliases.constEnd() && old->code != alias.code)
            m_byCode.remove(old->code);

        m_aliases.insert(alias.id, alias);
        m_byCode.insert(alias.code, alias.id);
    }

    std::optional<RoomAlias> findById(int id) const override
    {
        const auto it = m_aliases.constFind(id);
        if (it == m_aliases.constEnd())
            return std::nullopt;
        return *it;
    }

    std::optional<RoomAlias> findByCode(const QString &code) const override
    {
        const auto it = m_byCode.constFind(code);
        if (it == m_byCode.constEnd())
            return std::nullopt;
        return findById(*it);
    }

    QList<RoomAlias> listByRoom(int roomId) const override
    {
        QList<RoomAlias> out;
        for (const RoomAlias &a : m_aliases)
            if (a.roomId == roomId)
                out.append(a);
        std::sort(out.begin(), out.end(),
                  [](const RoomAlias &a, const RoomAlias &b) { return a.id < b.id; });
        return out;
    }

    bool removeBy(int id) override
    {
        const auto it = m_aliases.constFind(id);
        if (it == m_aliases.constEnd())
            return false;
        m_byCode.remove(it->code);
        m_aliases.remove(id);
        return true;
    }

private:
    QHash<int, RoomAlias> m_aliases;
    QHash<QString, int> m_byCode;
    int m_nextId = 1;
};
