#pragma once

#include <QJsonObject>
#include <QString>

// Личная комната пользователя: постоянный код-ссылка (?room=vi5), название
// и необязательный пароль. Одна на пользователя. id == -1 — ещё не сохранена.
//
// Пароль хранится открытым текстом сознательно: владелец должен уметь его
// «посмотреть» в настройках (согласовано в дорожной карте). Это пароль
// комнаты, а не аккаунта — аккаунтные пароли живут только как PBKDF2.
struct PersonalRoom
{
    int     id = -1;
    int     ownerId = -1;    // id пользователя-владельца (User::id)
    QString code;            // нормализован: lowercase, 3–32, [a-z0-9_-]
    QString title;           // 1–64 символа; видят участники конференции
    QString password;        // пусто — вход свободный
    qint64  createdAtMs = 0;

    // Представление для владельца: он видит всё, включая пароль.
    QJsonObject ownerJson() const
    {
        return QJsonObject{
            {"code", code},
            {"title", title},
            {"password", password},
        };
    }
};
