#include "services/PersonalRoomService.h"

#include <QDateTime>
#include <QRegularExpression>

PersonalRoomService::PersonalRoomService(std::shared_ptr<IPersonalRooms> rooms)
    : m_rooms(std::move(rooms))
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
    if (m_rooms->findByCode(code).has_value())
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
        if (other.has_value() && other->id != room.id)
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
