#pragma once

#include <QHash>

#include "interface/Abstract/IUsers.h"

// Пользователи в памяти: живут до перезапуска сервера. Второй хеш — индекс
// по логину, чтобы вход не сканировал всех пользователей.
class InMemoryUsers : public IUsers
{
public:
    void save(User &user) override
    {
        if (user.id == -1)
            user.id = m_nextId++;
        else if (user.id >= m_nextId)
            m_nextId = user.id + 1;

        const auto old = m_users.constFind(user.id);
        if (old != m_users.constEnd() && old->login != user.login)
            m_byLogin.remove(old->login);

        m_users.insert(user.id, user);
        m_byLogin.insert(user.login, user.id);
    }

    std::optional<User> findById(int id) const override
    {
        const auto it = m_users.constFind(id);
        if (it == m_users.constEnd())
            return std::nullopt;
        return *it;
    }

    std::optional<User> findByLogin(const QString &login) const override
    {
        const auto it = m_byLogin.constFind(login);
        if (it == m_byLogin.constEnd())
            return std::nullopt;
        return findById(*it);
    }

    bool removeBy(int id) override
    {
        const auto it = m_users.constFind(id);
        if (it == m_users.constEnd())
            return false;
        m_byLogin.remove(it->login);
        m_users.remove(id);
        return true;
    }

private:
    QHash<int, User> m_users;
    QHash<QString, int> m_byLogin;
    int m_nextId = 1;
};
