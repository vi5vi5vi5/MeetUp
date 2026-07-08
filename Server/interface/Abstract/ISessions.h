#pragma once

#include <optional>

#include "models/Session.h"

// Хранилище сессий входа. Ключ — токен из куки.
class ISessions
{
public:
    virtual ~ISessions() = default;

    // Вставка или обновление по token (продление срока действия).
    virtual void save(const Session &session) = 0;

    virtual std::optional<Session> findByToken(const QString &token) const = 0;

    virtual bool removeByToken(const QString &token) = 0;

    // Разлогин со всех устройств (пригодится для смены пароля).
    virtual int removeByUser(int userId) = 0;

    // Чистка протухших сессий; возвращает число удалённых.
    virtual int removeExpired(qint64 nowMs) = 0;
};
