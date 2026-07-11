#include "services/PersonalRoomService.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QRegularExpression>

PersonalRoomService::PersonalRoomService(std::shared_ptr<IPersonalRooms> rooms,
                                         std::shared_ptr<IRoomAliases> aliases)
    : m_rooms(std::move(rooms)), m_aliases(std::move(aliases))
{
}

QString PersonalRoomService::normalizeCode(const QString &raw)
{
    return raw.trimmed().toLower();
}

bool PersonalRoomService::validCode(const QString &code)
{
    // Код живёт в ссылке (?room=vi5): короткий, без экранирования, без
    // пробелов. Регистр не различается — normalizeCode приводит к нижнему.
    static const QRegularExpression re(QStringLiteral("^[a-z0-9_-]{3,32}$"));
    return re.match(code).hasMatch();
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
    if (m_rooms->findByOwner(ownerId).has_value())
        return RoomResult::fail("room_exists");

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

RoomResult PersonalRoomService::update(int ownerId,
                                       const std::optional<QString> &rawCode,
                                       const std::optional<QString> &rawTitle,
                                       const std::optional<QString> &password)
{
    std::optional<PersonalRoom> existing = m_rooms->findByOwner(ownerId);
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

bool PersonalRoomService::remove(int ownerId)
{
    const std::optional<PersonalRoom> room = m_rooms->findByOwner(ownerId);
    if (!room.has_value())
        return false;
    return m_rooms->removeBy(room->id);
}

std::optional<PersonalRoom> PersonalRoomService::byOwner(int ownerId) const
{
    return m_rooms->findByOwner(ownerId);
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

AliasResult PersonalRoomService::createAlias(int ownerId, const QString &password,
                                             int usesLeft, const QStringList &rawLogins,
                                             bool enabled)
{
    const std::optional<PersonalRoom> room = m_rooms->findByOwner(ownerId);
    if (!room.has_value())
        return AliasResult::fail("no_room");
    if (m_aliases->listByRoom(room->id).size() >= kMaxAliases)
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
    // Алиас должен принадлежать комнате именно этого владельца — чужой id
    // неотличим от несуществующего.
    const std::optional<PersonalRoom> room = m_rooms->findByOwner(ownerId);
    std::optional<RoomAlias> existing = m_aliases->findById(aliasId);
    if (!room.has_value() || !existing.has_value() || existing->roomId != room->id)
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
    const std::optional<PersonalRoom> room = m_rooms->findByOwner(ownerId);
    const std::optional<RoomAlias> alias = m_aliases->findById(aliasId);
    if (!room.has_value() || !alias.has_value() || alias->roomId != room->id)
        return false;
    return m_aliases->removeBy(aliasId);
}

QList<RoomAlias> PersonalRoomService::aliasesByOwner(int ownerId) const
{
    const std::optional<PersonalRoom> room = m_rooms->findByOwner(ownerId);
    if (!room.has_value())
        return {};
    return m_aliases->listByRoom(room->id);
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
