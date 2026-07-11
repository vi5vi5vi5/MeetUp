#pragma once

#include <memory>
#include <optional>

#include <QString>
#include <QStringList>

#include "interface/Abstract/IPersonalRooms.h"
#include "interface/Abstract/IRoomAliases.h"

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

// То же для alias-ссылок: "no_room", "no_alias", "alias_limit",
// "invalid_password", "invalid_uses", "invalid_logins".
struct AliasResult
{
    bool ok = false;
    QString error;
    RoomAlias alias;

    static AliasResult fail(const char *code)
    {
        AliasResult r;
        r.error = QLatin1String(code);
        return r;
    }
};

// Личные комнаты: создание, настройка и удаление. Правила: комната одна на
// пользователя, код уникален среди личных комнат, пароль хранится открытым
// текстом (владелец должен уметь его посмотреть). Про HTTP и WebSocket этот
// класс не знает; кто владелец — решает вызывающий по сессии.
//
// Здесь же живут alias-ссылки комнаты: до kMaxAliases на комнату, со своим
// паролем, лимитом использований и списком допущенных логинов.
class PersonalRoomService
{
public:
    PersonalRoomService(std::shared_ptr<IPersonalRooms> rooms,
                        std::shared_ptr<IRoomAliases> aliases);

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
    std::optional<PersonalRoom> byId(int id) const;

    // ---- Alias-ссылки (все операции владельца — от его ownerId) ----

    AliasResult createAlias(int ownerId, const QString &password, int usesLeft,
                            const QStringList &rawLogins, bool enabled);

    // Частичное обновление, как у update(): nullopt — поле не трогаем.
    AliasResult updateAlias(int ownerId, int aliasId,
                            const std::optional<QString> &password,
                            const std::optional<int> &usesLeft,
                            const std::optional<QStringList> &rawLogins,
                            const std::optional<bool> &enabled);

    bool removeAlias(int ownerId, int aliasId);
    QList<RoomAlias> aliasesByOwner(int ownerId) const;
    std::optional<RoomAlias> aliasByCode(const QString &rawCode) const;

    // Успешный вход по ссылке: минус одно использование; на нуле алиас
    // удаляется. Безлимитные (-1) не трогаем.
    void consumeAlias(const RoomAlias &alias);

    static QString normalizeCode(const QString &raw);
    static bool validCode(const QString &code);
    static bool validTitle(const QString &title);
    static bool validPassword(const QString &password);

    static constexpr int kMaxAliases = 5;

private:
    QString generateAliasCode() const;
    static std::optional<QStringList> normalizeLogins(const QStringList &raw);

    std::shared_ptr<IPersonalRooms> m_rooms;
    std::shared_ptr<IRoomAliases> m_aliases;
};
