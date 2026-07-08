#pragma once

#include <QHash>

#include "interface/Abstract/ISessions.h"

// Сессии в памяти: после перезапуска сервера все выходят из аккаунтов.
// С переходом на SQLite сессии переживут рестарт.
class InMemorySessions : public ISessions
{
public:
    void save(const Session &session) override
    {
        m_sessions.insert(session.token, session);
    }

    std::optional<Session> findByToken(const QString &token) const override
    {
        const auto it = m_sessions.constFind(token);
        if (it == m_sessions.constEnd())
            return std::nullopt;
        return *it;
    }

    bool removeByToken(const QString &token) override
    {
        return m_sessions.remove(token) > 0;
    }

    int removeByUser(int userId) override
    {
        int removed = 0;
        for (auto it = m_sessions.begin(); it != m_sessions.end(); ) {
            if (it->userId == userId) {
                it = m_sessions.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }

    int removeExpired(qint64 nowMs) override
    {
        int removed = 0;
        for (auto it = m_sessions.begin(); it != m_sessions.end(); ) {
            if (it->isExpired(nowMs)) {
                it = m_sessions.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }

private:
    QHash<QString, Session> m_sessions;
};
