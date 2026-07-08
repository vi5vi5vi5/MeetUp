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

bool HttpApi::route(const HttpRequest &req, const Respond &respond)
{
    const QString &p = req.path;
    if (p != QLatin1String("/api") && !p.startsWith(QLatin1String("/api/")))
        return false;

    const QByteArray &m = req.method;

    if (p == QLatin1String("/api/auth/register")) {
        if (m == "POST") handleRegister(req, respond);
        else respond(err(405, QStringLiteral("method_not_allowed")));
        return true;
    }
    if (p == QLatin1String("/api/auth/login")) {
        if (m == "POST") handleLogin(req, respond);
        else respond(err(405, QStringLiteral("method_not_allowed")));
        return true;
    }
    if (p == QLatin1String("/api/auth/logout")) {
        respond(m == "POST" ? handleLogout(req) : err(405, QStringLiteral("method_not_allowed")));
        return true;
    }

    if (p == QLatin1String("/api/me")) {
        if (m == "GET")        respond(handleMe(req));
        else if (m == "PATCH") respond(handlePatchMe(req));
        else respond(err(405, QStringLiteral("method_not_allowed")));
        return true;
    }

    if (p == QLatin1String("/api/rooms")) {
        respond(m == "POST" ? handleCreateRoom() : err(405, QStringLiteral("method_not_allowed")));
        return true;
    }
    if (p.startsWith(QLatin1String("/api/rooms/"))) {
        respond(m == "GET" ? handleCheckRoom(p.mid(int(qstrlen("/api/rooms/"))))
                           : err(405, QStringLiteral("method_not_allowed")));
        return true;
    }

    respond(err(404, QStringLiteral("unknown_endpoint")));
    return true;
}

// ---------- Аккаунты ----------

// Ответ с пользователем и свежей кукой сессии (register и login).
static ApiResponse sessionResponse(const AuthResult &res, const QByteArray &cookie)
{
    ApiResponse resp;
    resp.body = QJsonObject{{"user", res.user.publicJson()}};
    resp.headers.append({QByteArrayLiteral("Set-Cookie"), cookie});
    return resp;
}

void HttpApi::handleRegister(const HttpRequest &req, const Respond &respond)
{
    bool okJson = false;
    const QJsonObject body = req.jsonBody(&okJson);
    if (!okJson) {
        respond(err(400, QStringLiteral("invalid_json")));
        return;
    }

    m_auth->registerUserAsync(
        body.value(QLatin1String("login")).toString(),
        body.value(QLatin1String("password")).toString(),
        body.value(QLatin1String("display_name")).toString(),
        [respond](const AuthResult &res) {
            if (!res.ok) {
                respond(err(statusForError(res.error), res.error));
                return;
            }
            qInfo().noquote() << QStringLiteral("auth: registered '%1' (id=%2)")
                                     .arg(res.user.login).arg(res.user.id);
            respond(sessionResponse(res, sessionCookie(res.session.token, kCookieMaxAgeS)));
        });
}

void HttpApi::handleLogin(const HttpRequest &req, const Respond &respond)
{
    bool okJson = false;
    const QJsonObject body = req.jsonBody(&okJson);
    if (!okJson) {
        respond(err(400, QStringLiteral("invalid_json")));
        return;
    }

    m_auth->loginAsync(
        body.value(QLatin1String("login")).toString(),
        body.value(QLatin1String("password")).toString(),
        [respond](const AuthResult &res) {
            if (!res.ok) {
                respond(err(statusForError(res.error), res.error));
                return;
            }
            respond(sessionResponse(res, sessionCookie(res.session.token, kCookieMaxAgeS)));
        });
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
