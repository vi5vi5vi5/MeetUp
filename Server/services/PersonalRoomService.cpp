#include "services/PersonalRoomService.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QRegularExpression>

PersonalRoomService::PersonalRoomService(std::shared_ptr<IPersonalRooms> rooms,
                                         std::shared_ptr<IRoomAliases> aliases,
                                         RoomLimits limits)
    : m_rooms(std::move(rooms)), m_aliases(std::move(aliases)), m_limits(limits),
      m_codeRe(QStringLiteral("^[a-z0-9_-]{%1,32}$").arg(limits.codeMinLen))
{
}

QString PersonalRoomService::normalizeCode(const QString &raw)
{
    return raw.trimmed().toLower();
}

bool PersonalRoomService::validCode(const QString &code) const
{
    // Код живёт в ссылке (?room=vi5): короткий, без экранирования, без
    // пробелов. Регистр не различается — normalizeCode приводит к нижнему.
    // Нижнюю границу длины задаёт владелец сервера: на людном сервере
    // трёхбуквенные коды разбирают в первый же день.
    return m_codeRe.match(code).hasMatch();
}

bool PersonalRoomService::validTitle(const QString &title)
{
    return !title.isEmpty() && title.size() <= 64;
}

bool PersonalRoomService::validPassword(const QString &password)
{
    // Пустой пароль — «входа по паролю нет», это валидное состояние.
    return password.size() <= 128;
}

RoomResult PersonalRoomService::create(int ownerId, const QString &rawCode,
                                       const QString &rawTitle, const QString &password)
{
    // Код ошибки зависит от лимита, и это не мелочь. При лимите 1 отвечаем
    // ровно как раньше — "room_exists": старые клиенты знают этот код и
    // показывают «у вас уже есть комната». Появился бы новый код, и старый
    // клиент сказал бы «ошибка сервера» там, где ничего не сломалось.
    if (m_rooms->listByOwner(ownerId).size() >= m_limits.maxPerUser)
        return RoomResult::fail(m_limits.maxPerUser <= 1 ? "room_exists" : "room_limit");

    const QString code = normalizeCode(rawCode);
    if (!validCode(code))
        return RoomResult::fail("invalid_code");
    if (m_rooms->findByCode(code).has_value() || m_aliases->findByCode(code).has_value())
        return RoomResult::fail("code_taken");

    const QString title = rawTitle.trimmed();
    if (!validTitle(title))
        return RoomResult::fail("invalid_title");
    if (!validPassword(password))
        return RoomResult::fail("invalid_password");

    PersonalRoom room;
    room.ownerId = ownerId;
    room.code = code;
    room.title = title;
    room.password = password;
    room.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m_rooms->save(room);

    RoomResult res;
    res.ok = true;
    res.room = room;
    return res;
}

RoomResult PersonalRoomService::update(int ownerId, int roomId,
                                       const std::optional<QString> &rawCode,
                                       const std::optional<QString> &rawTitle,
                                       const std::optional<QString> &password)
{
    std::optional<PersonalRoom> existing = byOwnerAndId(ownerId, roomId);
    if (!existing.has_value())
        return RoomResult::fail("no_room");
    PersonalRoom room = *existing;

    if (rawCode.has_value()) {
        const QString code = normalizeCode(*rawCode);
        if (!validCode(code))
            return RoomResult::fail("invalid_code");
        const std::optional<PersonalRoom> other = m_rooms->findByCode(code);
        if ((other.has_value() && other->id != room.id)
            || m_aliases->findByCode(code).has_value())
            return RoomResult::fail("code_taken");
        room.code = code;
    }
    if (rawTitle.has_value()) {
        const QString title = rawTitle->trimmed();
        if (!validTitle(title))
            return RoomResult::fail("invalid_title");
        room.title = title;
    }
    if (password.has_value()) {
        if (!validPassword(*password))
            return RoomResult::fail("invalid_password");
        room.password = *password;
    }

    m_rooms->save(room);

    RoomResult res;
    res.ok = true;
    res.room = room;
    return res;
}

bool PersonalRoomService::remove(int ownerId, int roomId)
{
    const std::optional<PersonalRoom> room = byOwnerAndId(ownerId, roomId);
    if (!room.has_value())
        return false;
    return m_rooms->removeBy(room->id);
}

QList<PersonalRoom> PersonalRoomService::byOwner(int ownerId) const
{
    return m_rooms->listByOwner(ownerId);
}

std::optional<PersonalRoom> PersonalRoomService::firstByOwner(int ownerId) const
{
    const QList<PersonalRoom> all = m_rooms->listByOwner(ownerId);
    if (all.isEmpty())
        return std::nullopt;
    return all.first();
}

std::optional<PersonalRoom> PersonalRoomService::byOwnerAndId(int ownerId, int roomId) const
{
    const std::optional<PersonalRoom> room = m_rooms->findById(roomId);
    if (!room.has_value() || room->ownerId != ownerId)
        return std::nullopt;
    return room;
}

std::optional<PersonalRoom> PersonalRoomService::byCode(const QString &rawCode) const
{
    return m_rooms->findByCode(normalizeCode(rawCode));
}

std::optional<PersonalRoom> PersonalRoomService::byId(int id) const
{
    return m_rooms->findById(id);
}

// ---------- Alias-ссылки ----------

// Приводит список логинов к виду, в котором их хранит AuthService (trimmed,
// lowercase), выбрасывает пустые и дубли. nullopt — список не пролезает в
// лимиты (слишком много или слишком длинный логин).
std::optional<QStringList> PersonalRoomService::normalizeLogins(const QStringList &raw)
{
    QStringList out;
    for (const QString &l : raw) {
        const QString login = l.trimmed().toLower();
        if (login.isEmpty())
            continue;
        if (login.size() > 64 || login.contains(QLatin1Char(',')))
            return std::nullopt;
        if (!out.contains(login))
            out.append(login);
    }
    if (out.size() > 50)
        return std::nullopt;
    return out;
}

QString PersonalRoomService::generateAliasCode() const
{
    // Код нельзя подобрать перебором: 10 случайных символов из 36 от
    // криптостойкого источника (как коды разовых комнат в RoomRegistry).
    // Коллизии с комнатами и другими алиасами перегенерируем.
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (;;) {
        QString code;
        code.reserve(10);
        for (int i = 0; i < 10; ++i)
            code.append(QLatin1Char(alphabet[QRandomGenerator::system()->bounded(36)]));
        if (!m_aliases->findByCode(code).has_value()
            && !m_rooms->findByCode(code).has_value())
            return code;
    }
}

AliasResult PersonalRoomService::createAlias(int ownerId, int roomId, const QString &password,
                                             int usesLeft, const QStringList &rawLogins,
                                             bool enabled)
{
    const std::optional<PersonalRoom> room = byOwnerAndId(ownerId, roomId);
    if (!room.has_value())
        return AliasResult::fail("no_room");
    if (m_aliases->listByRoom(room->id).size() >= m_limits.maxAliases)
        return AliasResult::fail("alias_limit");

    if (!validPassword(password))
        return AliasResult::fail("invalid_password");
    if (usesLeft != -1 && (usesLeft < 1 || usesLeft > 1000000))
        return AliasResult::fail("invalid_uses");
    const std::optional<QStringList> logins = normalizeLogins(rawLogins);
    if (!logins.has_value())
        return AliasResult::fail("invalid_logins");

    RoomAlias alias;
    alias.roomId = room->id;
    alias.code = generateAliasCode();
    alias.password = password;
    alias.usesLeft = usesLeft;
    alias.logins = *logins;
    alias.enabled = enabled;
    alias.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m_aliases->save(alias);

    AliasResult res;
    res.ok = true;
    res.alias = alias;
    return res;
}

AliasResult PersonalRoomService::updateAlias(int ownerId, int aliasId,
                                             const std::optional<QString> &password,
                                             const std::optional<int> &usesLeft,
                                             const std::optional<QStringList> &rawLogins,
                                             const std::optional<bool> &enabled)
{
    // Алиас должен вести в комнату именно этого владельца — чужой id
    // неотличим от несуществующего.
    std::optional<RoomAlias> existing = m_aliases->findById(aliasId);
    if (!existing.has_value() || !roomOfAlias(ownerId, *existing).has_value())
        return AliasResult::fail("no_alias");
    RoomAlias alias = *existing;

    if (password.has_value()) {
        if (!validPassword(*password))
            return AliasResult::fail("invalid_password");
        alias.password = *password;
    }
    if (usesLeft.has_value()) {
        if (*usesLeft != -1 && (*usesLeft < 1 || *usesLeft > 1000000))
            return AliasResult::fail("invalid_uses");
        alias.usesLeft = *usesLeft;
    }
    if (rawLogins.has_value()) {
        const std::optional<QStringList> logins = normalizeLogins(*rawLogins);
        if (!logins.has_value())
            return AliasResult::fail("invalid_logins");
        alias.logins = *logins;
    }
    if (enabled.has_value())
        alias.enabled = *enabled;

    m_aliases->save(alias);

    AliasResult res;
    res.ok = true;
    res.alias = alias;
    return res;
}

bool PersonalRoomService::removeAlias(int ownerId, int aliasId)
{
    const std::optional<RoomAlias> alias = m_aliases->findById(aliasId);
    if (!alias.has_value() || !roomOfAlias(ownerId, *alias).has_value())
        return false;
    return m_aliases->removeBy(aliasId);
}

QList<RoomAlias> PersonalRoomService::aliasesByRoom(int ownerId, int roomId) const
{
    if (!byOwnerAndId(ownerId, roomId).has_value())
        return {};
    return m_aliases->listByRoom(roomId);
}

std::optional<PersonalRoom> PersonalRoomService::roomOfAlias(int ownerId,
                                                            const RoomAlias &alias) const
{
    return byOwnerAndId(ownerId, alias.roomId);
}

std::optional<RoomAlias> PersonalRoomService::aliasByCode(const QString &rawCode) const
{
    return m_aliases->findByCode(normalizeCode(rawCode));
}

void PersonalRoomService::consumeAlias(const RoomAlias &alias)
{
    // Перечитываем: счётчик могли уже подвинуть параллельные входы.
    std::optional<RoomAlias> fresh = m_aliases->findById(alias.id);
    if (!fresh.has_value() || fresh->usesLeft == -1)
        return;
    if (fresh->usesLeft <= 1) {
        m_aliases->removeBy(fresh->id);
        return;
    }
    fresh->usesLeft -= 1;
    m_aliases->save(*fresh);
}
