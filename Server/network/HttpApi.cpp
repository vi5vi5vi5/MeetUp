#include "network/HttpApi.h"

#include <QDebug>

#include "core/ConferenceRoom.h"
#include "core/RoomRegistry.h"
#include "network/HttpRequest.h"
#include "services/AuthService.h"

namespace {
const QByteArray kSessionCookieName = QByteArrayLiteral("meetup_session");
constexpr qint64 kCookieMaxAgeS = AuthService::kSessionTtlMs / 1000;
} // namespace

HttpApi::HttpApi(std::shared_ptr<AuthService> auth, RoomRegistry *rooms)
    : m_auth(std::move(auth)), m_rooms(rooms)
{
}

std::optional<ApiResponse> HttpApi::route(const HttpRequest &req)
{
    const QString &p = req.path;
    if (p != QLatin1String("/api") && !p.startsWith(QLatin1String("/api/")))
        return std::nullopt;

    const QByteArray &m = req.method;

    if (p == QLatin1String("/api/auth/register"))
        return m == "POST" ? handleRegister(req) : err(405, QStringLiteral("method_not_allowed"));
    if (p == QLatin1String("/api/auth/login"))
        return m == "POST" ? handleLogin(req) : err(405, QStringLiteral("method_not_allowed"));
    if (p == QLatin1String("/api/auth/logout"))
        return m == "POST" ? handleLogout(req) : err(405, QStringLiteral("method_not_allowed"));

    if (p == QLatin1String("/api/me")) {
        if (m == "GET")   return handleMe(req);
        if (m == "PATCH") return handlePatchMe(req);
        return err(405, QStringLiteral("method_not_allowed"));
    }

    if (p == QLatin1String("/api/rooms"))
        return m == "POST" ? handleCreateRoom() : err(405, QStringLiteral("method_not_allowed"));
    if (p.startsWith(QLatin1String("/api/rooms/")))
        return m == "GET" ? handleCheckRoom(p.mid(int(qstrlen("/api/rooms/"))))
                          : err(405, QStringLiteral("method_not_allowed"));

    return err(404, QStringLiteral("unknown_endpoint"));
}

// ---------- Аккаунты ----------

ApiResponse HttpApi::handleRegister(const HttpRequest &req)
{
    bool okJson = false;
    const QJsonObject body = req.jsonBody(&okJson);
    if (!okJson)
        return err(400, QStringLiteral("invalid_json"));

    const AuthResult res = m_auth->registerUser(body.value(QLatin1String("login")).toString(),
                                                body.value(QLatin1String("password")).toString(),
                                                body.value(QLatin1String("display_name")).toString());
    if (!res.ok)
        return err(statusForError(res.error), res.error);

    qInfo().noquote() << QStringLiteral("auth: registered '%1' (id=%2)")
                             .arg(res.user.login).arg(res.user.id);

    ApiResponse resp;
    resp.body = QJsonObject{{"user", res.user.publicJson()}};
    resp.headers.append({QByteArrayLiteral("Set-Cookie"),
                         sessionCookie(res.session.token, kCookieMaxAgeS)});
    return resp;
}

ApiResponse HttpApi::handleLogin(const HttpRequest &req)
{
    bool okJson = false;
    const QJsonObject body = req.jsonBody(&okJson);
    if (!okJson)
        return err(400, QStringLiteral("invalid_json"));

    const AuthResult res = m_auth->login(body.value(QLatin1String("login")).toString(),
                                         body.value(QLatin1String("password")).toString());
    if (!res.ok)
        return err(statusForError(res.error), res.error);

    ApiResponse resp;
    resp.body = QJsonObject{{"user", res.user.publicJson()}};
    resp.headers.append({QByteArrayLiteral("Set-Cookie"),
                         sessionCookie(res.session.token, kCookieMaxAgeS)});
    return resp;
}

ApiResponse HttpApi::handleLogout(const HttpRequest &req)
{
    m_auth->logout(sessionToken(req));

    // Max-Age=0 стирает куку в браузере независимо от того, была ли сессия.
    ApiResponse resp;
    resp.headers.append({QByteArrayLiteral("Set-Cookie"), sessionCookie(QString(), 0)});
    return resp;
}

ApiResponse HttpApi::handleMe(const HttpRequest &req)
{
    const std::optional<User> user = m_auth->userByToken(sessionToken(req));
    if (!user.has_value())
        return err(401, QStringLiteral("no_session"));
    return ApiResponse{200, QJsonObject{{"user", user->publicJson()}}, {}};
}

ApiResponse HttpApi::handlePatchMe(const HttpRequest &req)
{
    bool okJson = false;
    const QJsonObject body = req.jsonBody(&okJson);
    if (!okJson)
        return err(400, QStringLiteral("invalid_json"));
    if (!body.contains(QLatin1String("display_name")))
        return err(400, QStringLiteral("nothing_to_update"));

    const AuthResult res = m_auth->changeDisplayName(
        sessionToken(req), body.value(QLatin1String("display_name")).toString());
    if (!res.ok)
        return err(statusForError(res.error), res.error);
    return ApiResponse{200, QJsonObject{{"user", res.user.publicJson()}}, {}};
}

// ---------- Комнаты ----------

ApiResponse HttpApi::handleCreateRoom()
{
    ConferenceRoom *room = m_rooms->createRoom();
    qInfo().noquote() << QStringLiteral("API: room created '%1' (total %2)")
                             .arg(room->code()).arg(m_rooms->roomCount());
    return ApiResponse{200, QJsonObject{{"room", room->code()}}, {}};
}

ApiResponse HttpApi::handleCheckRoom(const QString &code)
{
    ConferenceRoom *room = m_rooms->find(code);
    if (!room)
        return err(404, QStringLiteral("room_not_found"));
    return ApiResponse{200, QJsonObject{
        {"room", room->code()},
        {"participants", int(room->sessions().size())},
    }, {}};
}

// ---------- Помощники ----------

ApiResponse HttpApi::err(int status, const QString &code)
{
    return ApiResponse{status, QJsonObject{{"error", code}}, {}};
}

int HttpApi::statusForError(const QString &code)
{
    if (code == QLatin1String("login_taken"))
        return 409;
    if (code == QLatin1String("wrong_credentials") || code == QLatin1String("no_session"))
        return 401;
    return 400;
}

QByteArray HttpApi::sessionCookie(const QString &token, qint64 maxAgeS)
{
    // HttpOnly — токен недоступен из JS; SameSite=Lax хватает: клиент и API
    // на одном origin. Secure не ставим: локальная разработка идёт по http,
    // а в проде до браузера куку доносит https-прокси.
    return kSessionCookieName + '=' + token.toUtf8()
            + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" + QByteArray::number(maxAgeS);
}

QString HttpApi::sessionToken(const HttpRequest &req)
{
    return QString::fromLatin1(req.cookie(kSessionCookieName));
}
