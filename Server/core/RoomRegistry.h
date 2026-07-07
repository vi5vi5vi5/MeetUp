#pragma once

#include <QHash>
#include <QString>

class ConferenceRoom;

// Реестр комнат: код -> комната. Владеет комнатами (создаёт и удаляет).
// На этом этапе комнаты живут только в памяти, без обращения к БД.
class RoomRegistry
{
public:
    RoomRegistry() = default;
    ~RoomRegistry();

    RoomRegistry(const RoomRegistry &) = delete;
    RoomRegistry &operator=(const RoomRegistry &) = delete;

    // Найти комнату по коду или создать новую, если её ещё нет.
    ConferenceRoom *getOrCreate(const QString &code);
    ConferenceRoom *find(const QString &code) const;

    // Удалить комнату, если в ней не осталось участников.
    void removeIfEmpty(ConferenceRoom *room);

    int roomCount() const { return int(m_rooms.size()); }

private:
    QHash<QString, ConferenceRoom *> m_rooms;
};
