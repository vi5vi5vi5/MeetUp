#pragma once

#include <QString>

// Сессия входа: токен живёт в HttpOnly-куке браузера, сервер по нему
// находит пользователя. Токен — 32 случайных байта в base64url, он же
// первичный ключ (id не нужен).
struct Session
{
    QString token;
    int     userId = -1;
    qint64  createdAtMs = 0;
    qint64  expiresAtMs = 0;

    bool isExpired(qint64 nowMs) const { return nowMs >= expiresAtMs; }
};
