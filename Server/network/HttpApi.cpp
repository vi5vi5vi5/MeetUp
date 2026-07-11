#include "network/HttpApi.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSaveFile>

#include "core/ClientSession.h"
#include "core/ConferenceRoom.h"
#include "core/RoomRegistry.h"
#include "network/HttpRequest.h"
#include "services/AuthService.h"
#include "services/PersonalRoomService.h"

namespace {
const QByteArray kSessionCookieName = QByteArrayLiteral("meetup_session");
constexpr qint64 kCookieMaxAgeS = AuthService::kSessionTtlMs / 1000;
} // namespace

HttpApi::HttpApi(std::shared_ptr<AuthService> auth,
                 std::shared_ptr<PersonalRoomService> personalRooms,
                 RoomRegistry *rooms, const QString &dataDir)
    : m_auth(std::move(auth)), m_personalRooms(std::move(personalRooms)), m_rooms(rooms),
      m_dataDir(dataDir)
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
    if (p == QLatin1String("/api/me/avatar")) {
        if (m == "POST" || m == "DELETE") respond(handleMyAvatar(req));
        else respond(err(405, QStringLiteral("method_not_allowed")));
        return true;
    }
    if (p.startsWith(QLatin1String("/api/users/")) && p.endsWith(QLatin1String("/avatar"))) {
        if (m == "GET") {
            const int base = int(qstrlen("/api/users/"));
            respond(handleUserAvatar(p.mid(base, p.size() - base - int(qstrlen("/avatar")))));
        } else {
            respond(err(405, QStringLiteral("method_not_allowed")));
        }
        return true;
    }

    if (p == QLatin1String("/api/me/room")) {
        respond(handleMyRoom(req));
        return true;
    }
    if (p == QLatin1String("/api/me/room/close")) {
        respond(m == "POST" ? handleCloseMyRoom(req) : err(405, QStringLiteral("method_not_allowed")));
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

// ---------- Аватарки ----------

QString HttpApi::avatarPath(int userId) const
{
    return QDir(m_dataDir).filePath(QStringLiteral("avatars/%1.jpg").arg(userId));
}

// POST {"image": "<base64 JPEG>"} — заменить; DELETE — убрать. Файл один на
// пользователя и перезаписывается на месте: старые аватарки не копятся.
ApiResponse HttpApi::handleMyAvatar(const HttpRequest &req)
{
    const QString token = sessionToken(req);
    const std::optional<User> user = m_auth->userByToken(token);
    if (!user.has_value())
        return err(401, QStringLiteral("no_session"));

    if (req.method == "DELETE") {
        QFile::remove(avatarPath(user->id));
        const AuthResult res = m_auth->setAvatarVer(token, 0);
        if (!res.ok)
            return err(statusForError(res.error), res.error);
        return ApiResponse{200, QJsonObject{{"user", res.user.publicJson()}}, {}};
    }

    bool okJson = false;
    const QJsonObject body = req.jsonBody(&okJson);
    if (!okJson)
        return err(400, QStringLiteral("invalid_json"));

    const auto decoded = QByteArray::fromBase64Encoding(
        body.value(QLatin1String("image")).toString().toLatin1(),
        QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded)
        return err(400, QStringLiteral("bad_image"));
    const QByteArray &img = *decoded;
    // Клиент шлёт JPEG (canvas.toDataURL); всё остальное — не наш файл.
    if (img.size() < 4 || img.size() > kMaxAvatarBytes
        || !img.startsWith(QByteArrayLiteral("\xFF\xD8\xFF")))
        return err(400, QStringLiteral("bad_image"));

    if (!QDir(m_dataDir).mkpath(QStringLiteral("avatars")))
        return err(500, QStringLiteral("storage_error"));
    QSaveFile file(avatarPath(user->id));
    if (!file.open(QIODevice::WriteOnly))
        return err(500, QStringLiteral("storage_error"));
    file.write(img);
    if (!file.commit())
        return err(500, QStringLiteral("storage_error"));

    const AuthResult res = m_auth->setAvatarVer(token, user->avatarVer + 1);
    if (!res.ok)
        return err(statusForError(res.error), res.error);
    return ApiResponse{200, QJsonObject{{"user", res.user.publicJson()}}, {}};
}

// Публичная раздача: URL versioned (?v=<avatar_ver>), поэтому кэш вечный —
// после замены клиент получает новую версию по новому URL.
ApiResponse HttpApi::handleUserAvatar(const QString &idStr)
{
    bool okId = false;
    const int id = idStr.toInt(&okId);
    if (!okId || id <= 0)
        return err(404, QStringLiteral("no_avatar"));

    QFile file(avatarPath(id));
    if (!file.open(QIODevice::ReadOnly))
        return err(404, QStringLiteral("no_avatar"));

    ApiResponse resp;
    resp.rawBody = file.readAll();
    resp.contentType = QByteArrayLiteral("image/jpeg");
    resp.cacheControl = QByteArrayLiteral("public, max-age=31536000, immutable");
    return resp;
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
    if (room && room->ownerId() < 0) {
        // Обычная разовая конференция.
        return ApiResponse{200, QJsonObject{
            {"room", room->code()},
            {"participants", int(room->sessions().size())},
        }, {}};
    }

    // Личная комната существует и вне эфира: клиенту нужны название,
    // активна ли она и есть ли пароль — чтобы спросить его до join.
    const std::optional<PersonalRoom> personal = m_personalRooms->byCode(code);
    if (!personal.has_value())
        return err(404, QStringLiteral("room_not_found"));

    QJsonObject j{
        {"room", personal->code},
        {"personal", true},
        {"title", personal->title},
        {"has_password", !personal->password.isEmpty()},
    };
    addLiveInfo(j, *personal, /*ownerView=*/false);
    return ApiResponse{200, j, {}};
}

// ---------- Личная комната владельца ----------

void HttpApi::addLiveInfo(QJsonObject &j, const PersonalRoom &room, bool ownerView) const
{
    ConferenceRoom *live = m_rooms->find(room.code);
    const bool online = live && !live->isEmpty();
    j.insert(QStringLiteral("online"), online);
    j.insert(QStringLiteral("participants"), online ? int(live->sessions().size()) : 0);
    if (!ownerView)
        return;

    // Владелец с главной видит, что происходит в комнате: кто внутри
    // и как давно идёт эфир.
    QJsonArray names;
    if (online) {
        for (ClientSession *s : live->sessions())
            names.append(s->name());
    }
    j.insert(QStringLiteral("participant_names"), names);
    j.insert(QStringLiteral("live_since_ms"), online ? live->liveSinceMs() : 0);
}

ApiResponse HttpApi::handleMyRoom(const HttpRequest &req)
{
    const std::optional<User> user = m_auth->userByToken(sessionToken(req));
    if (!user.has_value())
        return err(401, QStringLiteral("no_session"));

    // Ответ владельцу: комната целиком (включая пароль — он вправе его
    // посмотреть) плюс живое состояние эфира.
    const auto roomResponse = [this](const PersonalRoom &room) {
        QJsonObject j = room.ownerJson();
        addLiveInfo(j, room, /*ownerView=*/true);
        return ApiResponse{200, QJsonObject{{"room", j}}, {}};
    };

    const QByteArray &m = req.method;

    if (m == "GET") {
        const std::optional<PersonalRoom> room = m_personalRooms->byOwner(user->id);
        if (!room.has_value())
            return err(404, QStringLiteral("no_room"));
        return roomResponse(*room);
    }

    if (m == "DELETE") {
        if (!m_personalRooms->remove(user->id))
            return err(404, QStringLiteral("no_room"));
        qInfo().noquote() << QStringLiteral("API: personal room removed (owner id=%1)").arg(user->id);
        return ApiResponse{};
    }

    if (m == "POST" || m == "PATCH") {
        bool okJson = false;
        const QJsonObject body = req.jsonBody(&okJson);
        if (!okJson)
            return err(400, QStringLiteral("invalid_json"));

        // PATCH меняет только присланные поля; отсутствие поля — «не трогать».
        const auto field = [&body](const char *name) -> std::optional<QString> {
            if (!body.contains(QLatin1String(name)))
                return std::nullopt;
            return body.value(QLatin1String(name)).toString();
        };

        const RoomResult res = (m == "POST")
            ? m_personalRooms->create(user->id,
                                      body.value(QLatin1String("code")).toString(),
                                      body.value(QLatin1String("title")).toString(),
                                      body.value(QLatin1String("password")).toString())
            : m_personalRooms->update(user->id, field("code"), field("title"), field("password"));
        if (!res.ok)
            return err(statusForError(res.error), res.error);
        if (m == "POST")
            qInfo().noquote() << QStringLiteral("API: personal room '%1' created (owner id=%2)")
                                     .arg(res.room.code).arg(user->id);
        return roomResponse(res.room);
    }

    return err(405, QStringLiteral("method_not_allowed"));
}

ApiResponse HttpApi::handleCloseMyRoom(const HttpRequest &req)
{
    const std::optional<User> user = m_auth->userByToken(sessionToken(req));
    if (!user.has_value())
        return err(401, QStringLiteral("no_session"));
    const std::optional<PersonalRoom> room = m_personalRooms->byOwner(user->id);
    if (!room.has_value())
        return err(404, QStringLiteral("no_room"));

    // «Завершить»: выгнать всех. Сокеты закрываются вежливо, отключения
    // разберёт ConferenceServer; опустевшую комнату позже соберёт purge.
    int kicked = 0;
    if (ConferenceRoom *live = m_rooms->find(room->code)) {
        const QList<ClientSession *> sessions = live->sessions();   // копия: close() меняет список
        for (ClientSession *s : sessions) {
            s->sendJson(QJsonObject{{"type", "error"}, {"reason", "room_closed"}});
            s->close();
            ++kicked;
        }
    }
    qInfo().noquote() << QStringLiteral("API: personal room '%1' closed by owner, kicked %2")
                             .arg(room->code).arg(kicked);
    return ApiResponse{200, QJsonObject{{"kicked", kicked}}, {}};
}

// ---------- Помощники ----------

ApiResponse HttpApi::err(int status, const QString &code)
{
    return ApiResponse{status, QJsonObject{{"error", code}}, {}};
}

int HttpApi::statusForError(const QString &code)
{
    if (code == QLatin1String("login_taken") || code == QLatin1String("code_taken")
        || code == QLatin1String("room_exists"))
        return 409;
    if (code == QLatin1String("wrong_credentials") || code == QLatin1String("no_session"))
        return 401;
    if (code == QLatin1String("no_room"))
        return 404;
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
