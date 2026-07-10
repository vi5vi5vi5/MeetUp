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

// Ответ API: HTTP-статус, JSON-тело и дополнительные заголовки (Set-Cookie).
struct ApiResponse
{
    int status = 200;
    QJsonObject body;
    QList<QPair<QByteArray, QByteArray>> headers;
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
            RoomRegistry *rooms);

    // false — путь не из /api, пусть обрабатывает статика. true — маршрут
    // взят в работу и respond будет позван (возможно, уже позван).
    bool route(const HttpRequest &req, const Respond &respond);

private:
    void handleRegister(const HttpRequest &req, const Respond &respond);
    void handleLogin(const HttpRequest &req, const Respond &respond);
    ApiResponse handleLogout(const HttpRequest &req);
    ApiResponse handleMe(const HttpRequest &req);
    ApiResponse handlePatchMe(const HttpRequest &req);
    ApiResponse handleCreateRoom();
    ApiResponse handleCheckRoom(const QString &code);

    // Личная комната владельца: /api/me/room (GET/POST/PATCH/DELETE).
    ApiResponse handleMyRoom(const HttpRequest &req);

    // «В эфире» — владелец сейчас в комнате; гостей без него не пускают.
    bool personalRoomOnline(const PersonalRoom &room, int *participants = nullptr) const;

    static ApiResponse err(int status, const QString &code);
    static int statusForError(const QString &code);
    static QByteArray sessionCookie(const QString &token, qint64 maxAgeS);
    static QString sessionToken(const HttpRequest &req);

    std::shared_ptr<AuthService> m_auth;
    std::shared_ptr<PersonalRoomService> m_personalRooms;
    RoomRegistry *m_rooms;   // не владеет (общий с ConferenceServer)
};
