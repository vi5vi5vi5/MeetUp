#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Alias-ссылка на личную комнату: дополнительный код входа со своими
// правилами — кастомный пароль, лимит использований, список допущенных
// логинов, вкл/выкл. Ведёт в ту же комнату, но проверки при входе — свои.
// Кодов максимум 5 на комнату, код генерирует сервер (случайный).
//
// Пароль открытым текстом — по той же причине, что у PersonalRoom:
// владелец должен уметь его посмотреть.
struct RoomAlias
{
    int     id = -1;
    int     roomId = -1;     // PersonalRoom::id
    QString code;            // случайный, уникальный среди алиасов и комнат
    QString password;        // пусто — вход без пароля
    int     usesLeft = -1;   // -1 — без лимита; дойдя до 0, алиас удаляется
    QStringList logins;      // пусто — пускать всех; иначе только эти логины
    bool    enabled = true;  // выключенный алиас неотличим от несуществующего
    qint64  createdAtMs = 0;

    // Представление для владельца комнаты: он видит всё, включая пароль.
    QJsonObject ownerJson() const
    {
        return QJsonObject{
            {"id", id},
            {"code", code},
            {"password", password},
            {"uses_left", usesLeft},
            {"logins", QJsonArray::fromStringList(logins)},
            {"enabled", enabled},
        };
    }
};
