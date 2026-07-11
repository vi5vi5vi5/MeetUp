#pragma once

#include <functional>
#include <memory>

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>

class AuthService;
class PersonalRoomService;
class RoomRegistry;
struct HttpRequest;
struct PersonalRoom;

// Ответ API: HTTP-статус, JSON-тело и дополнительные заголовки (Set-Cookie).
// Если contentType непуст, вместо JSON уходит rawBody (бинарные аватарки).
struct ApiResponse
{
    int status = 200;
    QJsonObject body;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray rawBody;
    QByteArray contentType;
    QByteArray cacheControl;   // пусто — no-store, как у остальных ответов API
};

// Маршрутизация /api/*: разбирает запрос, зовёт сервисы, собирает JSON.
// Про сокеты не знает — транспортом занимается HttpFileServer. Бизнес-логики
// здесь тоже нет: только перевод HTTP <-> вызовы сервисов.
//
// Ответ уходит через respond-колбэк: register/login хешируют пароль в пуле
// потоков и отвечают позже, остальные маршруты зовут respond сразу.
class HttpApi
{
public:
    using Respond = std::function<void(const ApiResponse &)>;

    HttpApi(std::shared_ptr<AuthService> auth,
            std::shared_ptr<PersonalRoomService> personalRooms,
            RoomRegistry *rooms, const QString &dataDir);

    // false — путь не из /api, пусть обрабатывает статика. true — маршрут
    // взят в работу и respond будет позван (возможно, уже позван).
    bool route(const HttpRequest &req, const Respond &respond);

private:
    void handleRegister(const HttpRequest &req, const Respond &respond);
    void handleLogin(const HttpRequest &req, const Respond &respond);
    ApiResponse handleLogout(const HttpRequest &req);
    ApiResponse handleMe(const HttpRequest &req);
    ApiResponse handlePatchMe(const HttpRequest &req);

    // Аватарка: один файл на пользователя (<data>/avatars/<id>.jpg),
    // новая перезаписывает старую — истории нет. POST/DELETE — своя,
    // GET /api/users/<id>/avatar — публичная раздача с вечным кэшем
    // (URL меняется через ?v=<avatar_ver>).
    ApiResponse handleMyAvatar(const HttpRequest &req);
    ApiResponse handleUserAvatar(const QString &idStr);
    QString avatarPath(int userId) const;
    ApiResponse handleCreateRoom();
    ApiResponse handleCheckRoom(const QString &code);

    // Личная комната владельца: /api/me/room (GET/POST/PATCH/DELETE)
    // и /api/me/room/close («Завершить» — выгоняет всех участников).
    ApiResponse handleMyRoom(const HttpRequest &req);
    ApiResponse handleCloseMyRoom(const HttpRequest &req);

    // Alias-ссылки комнаты: /api/me/room/aliases (GET/POST)
    // и /api/me/room/aliases/<id> (PATCH/DELETE).
    ApiResponse handleMyAliases(const HttpRequest &req);
    ApiResponse handleMyAlias(const HttpRequest &req, const QString &idStr);

    // Живое состояние личной комнаты: online («в эфире» — внутри кто-то
    // есть), число участников; владельцу — ещё имена и старт эфира.
    void addLiveInfo(QJsonObject &j, const PersonalRoom &room, bool ownerView) const;

    static ApiResponse err(int status, const QString &code);
    static int statusForError(const QString &code);
    static QByteArray sessionCookie(const QString &token, qint64 maxAgeS);
    static QString sessionToken(const HttpRequest &req);

    std::shared_ptr<AuthService> m_auth;
    std::shared_ptr<PersonalRoomService> m_personalRooms;
    RoomRegistry *m_rooms;   // не владеет (общий с ConferenceServer)
    QString m_dataDir;       // персистентные данные (БД, аватарки)

    // Аватарка ужимается клиентом до 256px JPEG (~10–40 КБ); потолок
    // декодированного файла — защита от заливки гигантов.
    static constexpr int kMaxAvatarBytes = 256 * 1024;
};
