#pragma once

#include <QHash>
#include <QString>

class ConferenceRoom;

// Реестр комнат: код -> комната. Владеет комнатами (создаёт и удаляет).
// Коды комнат генерирует только сервер (см. createRoom) — join возможен лишь
// в существующую комнату. Комнаты живут в памяти, без обращения к БД.
class RoomRegistry
{
public:
    RoomRegistry() = default;
    ~RoomRegistry();

    RoomRegistry(const RoomRegistry &) = delete;
    RoomRegistry &operator=(const RoomRegistry &) = delete;

    // Создать комнату со случайным уникальным кодом.
    ConferenceRoom *createRoom();

    // Открыть личную комнату: код задан владельцем, комната помнит его id.
    // Если код занят живой комнатой — nullptr (join разберётся с существующей).
    ConferenceRoom *createPersonal(const QString &code, int ownerId);

    ConferenceRoom *find(const QString &code) const;

    // Удалить комнаты, простоявшие пустыми дольше ttlMs: брошенные после
    // создания и те, откуда все вышли. Пауза до удаления даёт пережить
    // обрыв связи последнего участника без потери комнаты и истории чата.
    // Возвращает число удалённых комнат.
    int purgeIdle(qint64 ttlMs);

    int roomCount() const { return int(m_rooms.size()); }

private:
    static QString generateCode();

    QHash<QString, ConferenceRoom *> m_rooms;
};
