#pragma once

#include <memory>
#include <optional>

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>

class AuthService;
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
class HttpApi
{
public:
    HttpApi(std::shared_ptr<AuthService> auth, RoomRegistry *rooms);

    // std::nullopt — путь не из /api, пусть обрабатывает статика.
    std::optional<ApiResponse> route(const HttpRequest &req);

private:
    ApiResponse handleRegister(const HttpRequest &req);
    ApiResponse handleLogin(const HttpRequest &req);
    ApiResponse handleLogout(const HttpRequest &req);
    ApiResponse handleMe(const HttpRequest &req);
    ApiResponse handlePatchMe(const HttpRequest &req);
    ApiResponse handleCreateRoom();
    ApiResponse handleCheckRoom(const QString &code);

    static ApiResponse err(int status, const QString &code);
    static int statusForError(const QString &code);
    static QByteArray sessionCookie(const QString &token, qint64 maxAgeS);
    static QString sessionToken(const HttpRequest &req);

    std::shared_ptr<AuthService> m_auth;
    RoomRegistry *m_rooms;   // не владеет (общий с ConferenceServer)
};
