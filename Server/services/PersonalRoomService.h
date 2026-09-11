#pragma once

#include <memory>
#include <optional>

#include <QRegularExpression>
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

// Пределы от владельца сервера. Как и AuthLimits: умолчания повторяют
// прежнее поведение, перевод «файл -> числа» делает main.
struct RoomLimits
{
    int codeMinLen = 3;    // короче — код не занять
    int maxAliases = 5;    // ссылок-приглашений на комнату
    int maxPerUser = 1;    // личных комнат на человека
};

// Личные комнаты: создание, настройка и удаление. Правила: комнат у человека
// до RoomLimits::maxPerUser, код уникален среди личных комнат, пароль хранится
// открытым текстом (владелец должен уметь его посмотреть). Про HTTP и
// WebSocket этот класс не знает; кто владелец — решает вызывающий по сессии.
//
// Все операции над конкретной комнатой берут roomId и сами проверяют, что она
// принадлежит этому владельцу: чужой id обязан быть неотличим от
// несуществующего, иначе по ответам можно перебрать чужие комнаты.
//
// Здесь же живут alias-ссылки комнаты: до RoomLimits::maxAliases на комнату, со своим
// паролем, лимитом использований и списком допущенных логинов.
class PersonalRoomService
{
public:
    PersonalRoomService(std::shared_ptr<IPersonalRooms> rooms,
                        std::shared_ptr<IRoomAliases> aliases,
                        RoomLimits limits = {});

    RoomResult create(int ownerId, const QString &rawCode,
                      const QString &rawTitle, const QString &password);

    // Частичное обновление: nullopt — поле не трогаем. Пустой пароль — убрать.
    RoomResult update(int ownerId, int roomId,
                      const std::optional<QString> &rawCode,
                      const std::optional<QString> &rawTitle,
                      const std::optional<QString> &password);

    bool remove(int ownerId, int roomId);

    // Все комнаты владельца, в порядке создания.
    QList<PersonalRoom> byOwner(int ownerId) const;
    // Первая (самая старая) комната. Через неё работают старые клиенты: они
    // знают ровно про одну комнату и ходят в /api/me/room без номера.
    std::optional<PersonalRoom> firstByOwner(int ownerId) const;
    // Конкретная комната этого владельца; чужая — nullopt.
    std::optional<PersonalRoom> byOwnerAndId(int ownerId, int roomId) const;
    std::optional<PersonalRoom> byCode(const QString &rawCode) const;
    std::optional<PersonalRoom> byId(int id) const;

    // Сколько комнат разрешено — клиенту это нужно, чтобы показать «3 из 3».
    int maxPerUser() const { return m_limits.maxPerUser; }

    // ---- Alias-ссылки (все операции владельца — от его ownerId) ----

    AliasResult createAlias(int ownerId, int roomId, const QString &password, int usesLeft,
                            const QStringList &rawLogins, bool enabled);

    // Частичное обновление, как у update(): nullopt — поле не трогаем.
    AliasResult updateAlias(int ownerId, int aliasId,
                            const std::optional<QString> &password,
                            const std::optional<int> &usesLeft,
                            const std::optional<QStringList> &rawLogins,
                            const std::optional<bool> &enabled);

    bool removeAlias(int ownerId, int aliasId);
    QList<RoomAlias> aliasesByRoom(int ownerId, int roomId) const;
    std::optional<RoomAlias> aliasByCode(const QString &rawCode) const;

    // Успешный вход по ссылке: минус одно использование; на нуле алиас
    // удаляется. Безлимитные (-1) не трогаем.
    void consumeAlias(const RoomAlias &alias);

    static QString normalizeCode(const QString &raw);
    // Не статический: нижняя граница длины — настройка сервера, и выражение
    // для проверки собирается один раз в конструкторе.
    bool validCode(const QString &code) const;
    static bool validTitle(const QString &title);
    static bool validPassword(const QString &password);


private:
    // Комната алиаса, если она принадлежит этому владельцу. Общая проверка
    // для updateAlias/removeAlias: раньше владение проверялось «единственной»
    // комнатой, теперь их несколько.
    std::optional<PersonalRoom> roomOfAlias(int ownerId, const RoomAlias &alias) const;

    QString generateAliasCode() const;
    static std::optional<QStringList> normalizeLogins(const QStringList &raw);

    std::shared_ptr<IPersonalRooms> m_rooms;
    std::shared_ptr<IRoomAliases> m_aliases;
    RoomLimits m_limits;
    QRegularExpression m_codeRe;   // собрано по m_limits.codeMinLen
};
