#include "network/HttpApi.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSaveFile>

#include "config/Log.h"
#include "config/ServerConfig.h"
#include "core/ClientSession.h"
#include "core/ConferenceRoom.h"
#include "core/RoomRegistry.h"
#include "network/HttpRequest.h"
#include "services/AuthService.h"
#include "services/PersonalRoomService.h"

namespace {
const QByteArray kSessionCookieName = QByteArrayLiteral("meetup_session");
} // namespace

HttpApi::HttpApi(std::shared_ptr<AuthService> auth,
                 std::shared_ptr<PersonalRoomService> personalRooms,
                 RoomRegistry *rooms, const QString &dataDir,
                 const ServerConfig &config)
    : m_auth(std::move(auth)), m_personalRooms(std::move(personalRooms)), m_rooms(rooms),
      m_dataDir(dataDir), m_config(config)
{
}

bool HttpApi::route(const HttpRequest &req, const Respond &respond)
{
    const QString &p = req.path;
    if (p != QLatin1String("/api") && !p.startsWith(QLatin1String("/api/")))
        return false;

    const QByteArray &m = req.method;

    if (p == QLatin1String("/api/config")) {
        respond(m == "GET" ? handleConfig() : err(405, QStringLiteral("method_not_allowed")));
        return true;
    }

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

    if (p == QLatin1String("/api/me/rooms")) {
        respond(handleMyRooms(req));
        return true;
    }
    if (p.startsWith(QLatin1String("/api/me/rooms/"))) {
        // Дальше идёт /<id>[/close | /aliases[/<aliasId>]].
        const QStringList rest =
            p.mid(int(qstrlen("/api/me/rooms/"))).split(QLatin1Char('/'));
        bool okId = false;
        const int roomId = rest.value(0).toInt(&okId);
        if (!okId) {
            respond(err(404, QStringLiteral("no_room")));
            return true;
        }
        const QString tail = rest.value(1);
        if (rest.size() == 1) {
            respond(handleMyRoom(req, roomId));
        } else if (rest.size() == 2 && tail == QLatin1String("close")) {
            respond(m == "POST" ? handleCloseMyRoom(req, roomId)
                                : err(405, QStringLiteral("method_not_allowed")));
        } else if (rest.size() == 2 && tail == QLatin1String("aliases")) {
            respond(handleMyAliases(req, roomId));
        } else if (rest.size() == 3 && tail == QLatin1String("aliases")) {
            respond(handleMyAlias(req, rest.value(2)));
        } else {
            respond(err(404, QStringLiteral("unknown_endpoint")));
        }
        return true;
    }

    // Старые клиенты: /api/me/room без номера — та же комната, первая.
    if (routeLegacyRoom(req, respond))
        return true;

    if (p == QLatin1String("/api/rooms")) {
        respond(m == "POST" ? handleCreateRoom(req) : err(405, QStringLiteral("method_not_allowed")));
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

// ---------- Портрет сервера ----------

// Всё, что клиенту нужно знать про сервер до входа. Отвечаем без авторизации:
// эти сведения всё равно видны по поведению сервера, а без них клиент рисует
// кнопки, которые заведомо ответят отказом.
//
// Про version — важная оговорка. Это то, что сервер СООБЩАЕТ о себе, а не то,
// что кто-то проверил: бинарь исполняется на чужой машине, и правка одной
// строки заставит его сказать что угодно. Годится как диагностика («у вас
// сборка трёхмесячной давности»), не годится как гарантия. Клиент обязан
// подписывать это соответственно.
ApiResponse HttpApi::handleConfig() const
{
    QJsonObject version{
        {"commit", ServerConfig::buildCommit()},
        {"modified", ServerConfig::buildModified()},
    };
    const QString builtAt = ServerConfig::buildTime();
    if (!builtAt.isEmpty())
        version.insert(QStringLiteral("built_at"), builtAt);

    QJsonObject j{
        {"name", m_config.name},
        {"version", version},
        {"web", m_config.webEnabled},
        // Честный ответ на вопрос «а вы записываете, кто к вам ходит».
        // Клиент показывает это человеку на гейте.
        {"logs_names", Log::keepsNames()},
        // Что разрешено. Клиент по этим полям решает, какие кнопки рисовать:
        // кнопка, которая всегда отвечает отказом, хуже её отсутствия.
        {"registration", m_config.registrationOpen},
        {"anonymous_join", m_config.allowAnonymousJoin},
        {"anonymous_rooms", m_config.anonymousCreateName()},
        // Правила, которые клиенту нужно знать ДО отправки формы, чтобы
        // сказать о них человеку, а не показывать ошибку задним числом.
        {"min_password_len", m_config.minPasswordLen},
        {"code_min_len", m_config.codeMinLen},
        {"max_aliases_per_room", m_config.maxAliasesPerRoom},
        {"max_personal_rooms", m_config.maxPersonalPerUser},
        {"max_screen_shares", m_config.maxScreenShares},
        {"chat_image_max_kb", m_config.chatImageMaxKb},
    };
    if (!m_config.publicUrl.isEmpty())
        j.insert(QStringLiteral("public_url"), m_config.publicUrl);
    return ApiResponse{200, j, {}};
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
    // Закрытая регистрация. Клиент об этом уже знает из GET /api/config и
    // кнопки не рисует — но ручка обязана отвечать отказом и без клиента.
    if (!m_config.registrationOpen) {
        respond(err(403, QStringLiteral("registration_closed")));
        return;
    }

    bool okJson = false;
    const QJsonObject body = req.jsonBody(&okJson);
    if (!okJson) {
        respond(err(400, QStringLiteral("invalid_json")));
        return;
    }

    // Кука живёт ровно столько же, сколько сессия на сервере: два разных
    // срока означали бы «вы разлогинены» при живой сессии или наоборот.
    const qint64 cookieMaxAgeS = m_auth->sessionTtlMs() / 1000;
    m_auth->registerUserAsync(
        body.value(QLatin1String("login")).toString(),
        body.value(QLatin1String("password")).toString(),
        body.value(QLatin1String("display_name")).toString(),
        [respond, cookieMaxAgeS](const AuthResult &res) {
            if (!res.ok) {
                respond(err(statusForError(res.error), res.error));
                return;
            }
            qInfo().noquote() << QStringLiteral("auth: registered '%1' (id=%2)")
                                     .arg(res.user.login).arg(res.user.id);
            respond(sessionResponse(res, sessionCookie(res.session.token, cookieMaxAgeS)));
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

    const qint64 cookieMaxAgeS = m_auth->sessionTtlMs() / 1000;
    m_auth->loginAsync(
        body.value(QLatin1String("login")).toString(),
        body.value(QLatin1String("password")).toString(),
        [respond, cookieMaxAgeS](const AuthResult &res) {
            if (!res.ok) {
                respond(err(statusForError(res.error), res.error));
                return;
            }
            respond(sessionResponse(res, sessionCookie(res.session.token, cookieMaxAgeS)));
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

ApiResponse HttpApi::handleCreateRoom(const HttpRequest &req)
{
    // Кто вправе завести разовую комнату. Ручка ничего не требовала, и на
    // открытом сервере с неё снимались тысячи комнат в минуту — это и есть
    // тот мусор, ради которого затевался конфиг.
    switch (m_config.anonymousCreate) {
    case ServerConfig::RoomCreate::Off:
        return err(403, QStringLiteral("rooms_closed"));
    case ServerConfig::RoomCreate::Account:
        if (!m_auth->userByToken(sessionToken(req)).has_value())
            return err(401, QStringLiteral("account_required"));
        break;
    case ServerConfig::RoomCreate::Open:
        break;
    }

    ConferenceRoom *room = m_rooms->createRoom();
    if (!room) {
        // Упёрлись в потолок. 503, а не 4xx: с запросом всё в порядке, это
        // серверу сейчас некуда, и через десять минут места снова будет.
        qWarning().noquote() << QStringLiteral("API: room limit reached (%1)")
                                    .arg(m_rooms->roomCount());
        return err(503, QStringLiteral("server_full"));
    }
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
    std::optional<PersonalRoom> personal = m_personalRooms->byCode(code);
    QString gateCode;
    bool hasPassword = false;
    if (personal.has_value()) {
        gateCode = personal->code;
        hasPassword = !personal->password.isEmpty();
    } else {
        // Alias-ссылка: ведёт в личную комнату, но пароль на гейте — свой.
        // join пойдёт по коду алиаса, чтобы сервер применил его правила.
        const std::optional<RoomAlias> alias = m_personalRooms->aliasByCode(code);
        if (alias.has_value() && alias->enabled)
            personal = m_personalRooms->byId(alias->roomId);
        if (!personal.has_value())
            return err(404, QStringLiteral("room_not_found"));
        gateCode = alias->code;
        hasPassword = !alias->password.isEmpty();
    }

    QJsonObject j{
        {"room", gateCode},
        {"personal", true},
        {"title", personal->title},
        {"has_password", hasPassword},
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

// Ответ владельцу: комната целиком (включая пароль — он вправе его
// посмотреть) плюс живое состояние эфира.
ApiResponse HttpApi::roomResponse(const PersonalRoom &room) const
{
    QJsonObject j = room.ownerJson();
    addLiveInfo(j, room, /*ownerView=*/true);
    return ApiResponse{200, QJsonObject{{"room", j}}, {}};
}

// Список комнат владельца и создание новой.
ApiResponse HttpApi::handleMyRooms(const HttpRequest &req)
{
    const std::optional<User> user = m_auth->userByToken(sessionToken(req));
    if (!user.has_value())
        return err(401, QStringLiteral("no_session"));

    if (req.method == "GET") {
        QJsonArray arr;
        for (const PersonalRoom &room : m_personalRooms->byOwner(user->id)) {
            QJsonObject j = room.ownerJson();
            addLiveInfo(j, room, /*ownerView=*/true);
            arr.append(j);
        }
        // max отдаём рядом со списком: клиенту он нужен ровно здесь — показать
        // «2 из 3» и погасить кнопку «добавить», не запрашивая конфиг отдельно.
        return ApiResponse{200, QJsonObject{
            {"rooms", arr},
            {"max", m_personalRooms->maxPerUser()},
        }, {}};
    }

    if (req.method == "POST") {
        bool okJson = false;
        const QJsonObject body = req.jsonBody(&okJson);
        if (!okJson)
            return err(400, QStringLiteral("invalid_json"));
        const RoomResult res = m_personalRooms->create(
            user->id,
            body.value(QLatin1String("code")).toString(),
            body.value(QLatin1String("title")).toString(),
            body.value(QLatin1String("password")).toString());
        if (!res.ok)
            return err(statusForError(res.error), res.error);
        qInfo().noquote() << QStringLiteral("API: personal room '%1' created (owner id=%2)")
                                 .arg(res.room.code).arg(user->id);
        return roomResponse(res.room);
    }

    return err(405, QStringLiteral("method_not_allowed"));
}

ApiResponse HttpApi::handleMyRoom(const HttpRequest &req, int roomId)
{
    const std::optional<User> user = m_auth->userByToken(sessionToken(req));
    if (!user.has_value())
        return err(401, QStringLiteral("no_session"));

    const QByteArray &m = req.method;

    if (m == "GET") {
        const std::optional<PersonalRoom> room =
            m_personalRooms->byOwnerAndId(user->id, roomId);
        if (!room.has_value())
            return err(404, QStringLiteral("no_room"));
        return roomResponse(*room);
    }

    if (m == "DELETE") {
        if (!m_personalRooms->remove(user->id, roomId))
            return err(404, QStringLiteral("no_room"));
        qInfo().noquote() << QStringLiteral("API: personal room removed (owner id=%1)").arg(user->id);
        return ApiResponse{};
    }

    if (m == "PATCH") {
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

        const RoomResult res = m_personalRooms->update(
            user->id, roomId, field("code"), field("title"), field("password"));
        if (!res.ok)
            return err(statusForError(res.error), res.error);
        return roomResponse(res.room);
    }

    return err(405, QStringLiteral("method_not_allowed"));
}

ApiResponse HttpApi::handleCloseMyRoom(const HttpRequest &req, int roomId)
{
    const std::optional<User> user = m_auth->userByToken(sessionToken(req));
    if (!user.has_value())
        return err(401, QStringLiteral("no_session"));
    const std::optional<PersonalRoom> room = m_personalRooms->byOwnerAndId(user->id, roomId);
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

// ---------- Alias-ссылки ----------

// Разбор общих полей тела алиаса: uses (число или null — безлимит),
// logins (массив строк), enabled (bool), password (строка).
static std::optional<int> aliasUsesField(const QJsonObject &body, bool *bad)
{
    if (!body.contains(QLatin1String("uses")))
        return std::nullopt;
    const QJsonValue v = body.value(QLatin1String("uses"));
    if (v.isNull())
        return -1;   // явное «без лимита»
    if (!v.isDouble()) {
        *bad = true;
        return std::nullopt;
    }
    return v.toInt();
}

static std::optional<QStringList> aliasLoginsField(const QJsonObject &body)
{
    if (!body.contains(QLatin1String("logins")))
        return std::nullopt;
    QStringList out;
    for (const QJsonValue &v : body.value(QLatin1String("logins")).toArray())
        out.append(v.toString());
    return out;
}

ApiResponse HttpApi::handleMyAliases(const HttpRequest &req, int roomId)
{
    const std::optional<User> user = m_auth->userByToken(sessionToken(req));
    if (!user.has_value())
        return err(401, QStringLiteral("no_session"));
    if (!m_personalRooms->byOwnerAndId(user->id, roomId).has_value())
        return err(404, QStringLiteral("no_room"));

    if (req.method == "GET") {
        QJsonArray arr;
        for (const RoomAlias &a : m_personalRooms->aliasesByRoom(user->id, roomId))
            arr.append(a.ownerJson());
        return ApiResponse{200, QJsonObject{{"aliases", arr}}, {}};
    }

    if (req.method == "POST") {
        bool okJson = false;
        const QJsonObject body = req.jsonBody(&okJson);
        if (!okJson)
            return err(400, QStringLiteral("invalid_json"));
        bool badUses = false;
        const std::optional<int> uses = aliasUsesField(body, &badUses);
        if (badUses)
            return err(400, QStringLiteral("invalid_uses"));
        const AliasResult res = m_personalRooms->createAlias(
            user->id, roomId,
            body.value(QLatin1String("password")).toString(),
            uses.value_or(-1),
            aliasLoginsField(body).value_or(QStringList{}),
            body.value(QLatin1String("enabled")).toBool(true));
        if (!res.ok)
            return err(statusForError(res.error), res.error);
        qInfo().noquote() << QStringLiteral("API: alias '%1' created (owner id=%2)")
                                 .arg(res.alias.code).arg(user->id);
        return ApiResponse{200, QJsonObject{{"alias", res.alias.ownerJson()}}, {}};
    }

    return err(405, QStringLiteral("method_not_allowed"));
}

ApiResponse HttpApi::handleMyAlias(const HttpRequest &req, const QString &idStr)
{
    const std::optional<User> user = m_auth->userByToken(sessionToken(req));
    if (!user.has_value())
        return err(401, QStringLiteral("no_session"));
    bool okId = false;
    const int aliasId = idStr.toInt(&okId);
    if (!okId)
        return err(404, QStringLiteral("no_alias"));

    if (req.method == "DELETE") {
        if (!m_personalRooms->removeAlias(user->id, aliasId))
            return err(404, QStringLiteral("no_alias"));
        return ApiResponse{};
    }

    if (req.method == "PATCH") {
        bool okJson = false;
        const QJsonObject body = req.jsonBody(&okJson);
        if (!okJson)
            return err(400, QStringLiteral("invalid_json"));
        bool badUses = false;
        const std::optional<int> uses = aliasUsesField(body, &badUses);
        if (badUses)
            return err(400, QStringLiteral("invalid_uses"));
        const std::optional<QString> password = body.contains(QLatin1String("password"))
            ? std::optional<QString>(body.value(QLatin1String("password")).toString())
            : std::nullopt;
        const std::optional<bool> enabled = body.contains(QLatin1String("enabled"))
            ? std::optional<bool>(body.value(QLatin1String("enabled")).toBool())
            : std::nullopt;
        const AliasResult res = m_personalRooms->updateAlias(
            user->id, aliasId, password, uses, aliasLoginsField(body), enabled);
        if (!res.ok)
            return err(statusForError(res.error), res.error);
        return ApiResponse{200, QJsonObject{{"alias", res.alias.ownerJson()}}, {}};
    }

    return err(405, QStringLiteral("method_not_allowed"));
}

// ---------- Совместимость со старыми клиентами ----------

// Скачанный .exe знает ровно одну личную комнату. Ему отвечаем про первую —
// самую старую: она не меняется от запроса к запросу (ORDER BY id в
// хранилище), и человек видит у старого клиента всегда одно и то же.
int HttpApi::firstRoomId(const HttpRequest &req) const
{
    const std::optional<User> user = m_auth->userByToken(sessionToken(req));
    if (!user.has_value())
        return -1;
    const std::optional<PersonalRoom> room = m_personalRooms->firstByOwner(user->id);
    return room.has_value() ? room->id : -1;
}

bool HttpApi::routeLegacyRoom(const HttpRequest &req, const Respond &respond)
{
    const QString &p = req.path;
    if (p != QLatin1String("/api/me/room") && !p.startsWith(QLatin1String("/api/me/room/")))
        return false;

    const QByteArray &m = req.method;

    // Создание — единственное место, где старая ручка НЕ переводится в новую.
    // Позволить ей завести вторую комнату значило бы дать клиенту создать то,
    // чего он сам показать не умеет: человек нажал бы «создать», получил успех
    // и не увидел результата. Поэтому здесь по-прежнему одна комната на
    // владельца, с прежним кодом ошибки.
    if (p == QLatin1String("/api/me/room") && m == "POST") {
        if (firstRoomId(req) >= 0) {
            respond(err(409, QStringLiteral("room_exists")));
            return true;
        }
        respond(handleMyRooms(req));
        return true;
    }

    const int roomId = firstRoomId(req);
    if (roomId < 0) {
        // Сессии нет или комнат нет — ответ тот же, что и раньше.
        const std::optional<User> user = m_auth->userByToken(sessionToken(req));
        respond(user.has_value() ? err(404, QStringLiteral("no_room"))
                                 : err(401, QStringLiteral("no_session")));
        return true;
    }

    if (p == QLatin1String("/api/me/room")) {
        respond(handleMyRoom(req, roomId));
    } else if (p == QLatin1String("/api/me/room/close")) {
        respond(m == "POST" ? handleCloseMyRoom(req, roomId)
                            : err(405, QStringLiteral("method_not_allowed")));
    } else if (p == QLatin1String("/api/me/room/aliases")) {
        respond(handleMyAliases(req, roomId));
    } else if (p.startsWith(QLatin1String("/api/me/room/aliases/"))) {
        respond(handleMyAlias(req, p.mid(int(qstrlen("/api/me/room/aliases/")))));
    } else {
        respond(err(404, QStringLiteral("unknown_endpoint")));
    }
    return true;
}

// ---------- Помощники ----------

ApiResponse HttpApi::err(int status, const QString &code)
{
    return ApiResponse{status, QJsonObject{{"error", code}}, {}};
}

int HttpApi::statusForError(const QString &code)
{
    if (code == QLatin1String("login_taken") || code == QLatin1String("code_taken")
        || code == QLatin1String("room_exists") || code == QLatin1String("alias_limit")
        || code == QLatin1String("room_limit"))
        return 409;
    if (code == QLatin1String("wrong_credentials") || code == QLatin1String("no_session"))
        return 401;
    if (code == QLatin1String("no_room") || code == QLatin1String("no_alias"))
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
