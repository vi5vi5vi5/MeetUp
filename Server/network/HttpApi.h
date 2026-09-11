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
struct ServerConfig;

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
            RoomRegistry *rooms, const QString &dataDir,
            const ServerConfig &config);

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
    ApiResponse handleCreateRoom(const HttpRequest &req);
    ApiResponse handleCheckRoom(const QString &code);

    // Публичный портрет сервера: имя, версия сборки, что на нём разрешено.
    // Без авторизации — клиент читает это ДО входа, чтобы не рисовать кнопки,
    // которые всё равно ответят отказом.
    ApiResponse handleConfig() const;

    // Личные комнаты владельца: /api/me/rooms (GET — список, POST — создать).
    ApiResponse handleMyRooms(const HttpRequest &req);

    // Одна комната: /api/me/rooms/<id> (GET/PATCH/DELETE) и .../close
    // («Завершить» — выгоняет всех участников).
    ApiResponse handleMyRoom(const HttpRequest &req, int roomId);
    ApiResponse handleCloseMyRoom(const HttpRequest &req, int roomId);

    // Alias-ссылки комнаты: /api/me/rooms/<id>/aliases (GET/POST)
    // и .../aliases/<aliasId> (PATCH/DELETE). Номер алиаса уникален сам по
    // себе, поэтому комнату для него повторно называть не нужно — владение
    // проверяет сервис.
    ApiResponse handleMyAliases(const HttpRequest &req, int roomId);
    ApiResponse handleMyAlias(const HttpRequest &req, const QString &idStr);

    // ---- Совместимость со старыми клиентами ----
    // Скачанный .exe знает ровно одну личную комнату и ходит в /api/me/room
    // без номера. Для него «комната» — первая (самая старая); остальные он не
    // видит и создать не может. Ломать его нельзя: обновляются не все и не сразу.
    bool routeLegacyRoom(const HttpRequest &req, const Respond &respond);
    // Номер первой комнаты владельца; -1 — комнат нет.
    int firstRoomId(const HttpRequest &req) const;

    // Комната целиком (включая пароль — владелец вправе его посмотреть)
    // плюс живое состояние эфира.
    ApiResponse roomResponse(const PersonalRoom &room) const;

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
    const ServerConfig &m_config;   // не владеет: живёт в main дольше нас

    // Аватарка ужимается клиентом до 256px JPEG (~10–40 КБ); потолок
    // декодированного файла — защита от заливки гигантов.
    static constexpr int kMaxAvatarBytes = 256 * 1024;
};
