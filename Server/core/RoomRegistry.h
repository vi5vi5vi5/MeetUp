#pragma once

#include <QHash>
#include <QString>

#include "core/ConferenceRoom.h"

// Реестр комнат: код -> комната. Владеет комнатами (создаёт и удаляет).
// Коды комнат генерирует только сервер (см. createRoom) — join возможен лишь
// в существующую комнату. Комнаты живут в памяти, без обращения к БД.
class RoomRegistry
{
public:
    // chat — потолки истории для новых комнат; maxOneOff — сколько разовых
    // комнат может жить одновременно (0 — без потолка).
    explicit RoomRegistry(ChatLimits chat = {}, int maxOneOff = 0);
    ~RoomRegistry();

    RoomRegistry(const RoomRegistry &) = delete;
    RoomRegistry &operator=(const RoomRegistry &) = delete;

    // Создать комнату со случайным уникальным кодом.
    // nullptr — упёрлись в потолок разовых комнат (HTTP-слой ответит 503).
    ConferenceRoom *createRoom();

    // Открыть личную комнату: код задан владельцем, комната помнит его id.
    // Если код занят живой комнатой — nullptr (join разберётся с существующей).
    //
    // Потолок разовых комнат сюда НЕ применяется намеренно: личных комнат не
    // может стать больше, чем аккаунтов, а запереть владельца снаружи из-за
    // того, что кто-то со стороны наплодил разовых, — худшее из возможных
    // поведений.
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
    ChatLimits m_chat;
    int m_maxOneOff = 0;

    // Разовых комнат сейчас живо. Считаем счётчиком, а не проходом по хешу:
    // проверка делается на каждое создание, а создание — это ровно та ручка,
    // по которой и приходит поток мусора.
    int m_oneOffCount = 0;
};
