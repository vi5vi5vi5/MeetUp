#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Пользователь MeetUp. id == -1 — ещё не сохранён (репозиторий присвоит id
// при save, как в MedFlow). Поля pass_* — только производные пароля:
// сам пароль нигде не хранится и восстановлению не подлежит.
struct User
{
    int        id = -1;
    QString    login;            // нормализован: lowercase, 3–32, [a-z0-9_.-]
    QString    displayName;      // 1–40 символов; видят участники конференции
    QString    passAlgo = QStringLiteral("pbkdf2-sha512");   // запас на миграцию алгоритма
    int        passIters = 0;
    QByteArray passSalt;         // 16 случайных байт на пользователя
    QByteArray passHash;         // 64 байта PBKDF2-HMAC-SHA512(password, salt, iters)
    int        avatarVer = 0;    // 0 — аватарки нет; растёт с каждой загрузкой
    qint64     createdAtMs = 0;

    // Представление для клиента: без секретов и служебных полей.
    QJsonObject publicJson() const
    {
        return QJsonObject{
            {"id", id},
            {"login", login},
            {"display_name", displayName},
            {"avatar_ver", avatarVer},
        };
    }
};
