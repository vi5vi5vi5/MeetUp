#pragma once

#include <memory>
#include <optional>

#include <QString>

#include "interface/Abstract/IPersonalRooms.h"

// Результат операции с личной комнатой: либо ok с данными, либо машинный
// код ошибки — сетевой слой превращает его в HTTP-статус, клиент — в текст.
struct RoomResult
{
    bool ok = false;
    QString error;      // "invalid_code", "code_taken", "room_exists",
                        // "invalid_title", "invalid_password", "no_room"
    PersonalRoom room;

    static RoomResult fail(const char *code)
    {
        RoomResult r;
        r.error = QLatin1String(code);
        return r;
    }
};

// Личные комнаты: создание, настройка и удаление. Правила: комната одна на
// пользователя, код уникален среди личных комнат, пароль хранится открытым
// текстом (владелец должен уметь его посмотреть). Про HTTP и WebSocket этот
// класс не знает; кто владелец — решает вызывающий по сессии.
class PersonalRoomService
{
public:
    explicit PersonalRoomService(std::shared_ptr<IPersonalRooms> rooms);

    RoomResult create(int ownerId, const QString &rawCode,
                      const QString &rawTitle, const QString &password);

    // Частичное обновление: nullopt — поле не трогаем. Пустой пароль — убрать.
    RoomResult update(int ownerId,
                      const std::optional<QString> &rawCode,
                      const std::optional<QString> &rawTitle,
                      const std::optional<QString> &password);

    bool remove(int ownerId);

    std::optional<PersonalRoom> byOwner(int ownerId) const;
    std::optional<PersonalRoom> byCode(const QString &rawCode) const;

    static QString normalizeCode(const QString &raw);
    static bool validCode(const QString &code);
    static bool validTitle(const QString &title);
    static bool validPassword(const QString &password);

private:
    std::shared_ptr<IPersonalRooms> m_rooms;
};
