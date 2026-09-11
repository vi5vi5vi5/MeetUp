#include "PersonalRoomController.h"
#include "ApiClient.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTimer>

PersonalRoomController::PersonalRoomController(ApiClient* api, QObject* parent)
    : QObject(parent), m_api(api) {}

void PersonalRoomController::setBusy(bool v) {
    if (m_busy == v) return; m_busy = v; emit busyChanged();
}
void PersonalRoomController::setError(const QString& t) {
    if (m_errorText == t) return; m_errorText = t; emit errorTextChanged();
}
void PersonalRoomController::setAliasError(const QString& t) {
    if (m_aliasError == t) return; m_aliasError = t; emit aliasErrorChanged();
}

// Тексты машинных кодов — словарь roomErrText() веба, один в один.
QString PersonalRoomController::errText(const QString& code) {
    if (code == "invalid_code")     return "Код: от 3 до 32 символов — латиница, цифры, «-» и «_».";
    if (code == "code_taken")       return "Этот код уже занят другой комнатой.";
    if (code == "room_exists")      return "У вас уже есть личная комната.";
    if (code == "invalid_title")    return "Название: от 1 до 64 символов.";
    if (code == "invalid_password") return "Пароль слишком длинный (до 128 символов).";
    if (code == "no_session")       return "Сессия истекла — войдите заново.";
    if (code == "no_room")          return "Комната уже удалена.";
    return "Не получилось сохранить. Попробуйте позже.";
}

// Тексты — словарь aliasErrText() веба.
QString PersonalRoomController::aliasErrText(const QString& code) {
    if (code == "alias_limit")      return "Больше ссылок на эту комнату сервер не разрешает — удалите одну.";
    if (code == "no_alias")         return "Ссылка уже удалена.";
    if (code == "invalid_uses")     return "Лимит использований — целое число от 1.";
    if (code == "invalid_logins")   return "Проверьте список логинов: до 50 логинов, каждый до 64 символов.";
    if (code == "invalid_password") return "Пароль слишком длинный (до 128 символов).";
    if (code == "no_session")       return "Сессия истекла — войдите заново.";
    if (code == "no_room")          return "Сначала создайте личную комнату.";
    return "Не получилось сохранить. Попробуйте позже.";
}

// Код комнаты по правилам сервера: lowercase, [a-z0-9_-], до 32 символов.
// (Веб делает то же самое в slugify() — пользователь может печатать что угодно.)
QString PersonalRoomController::slugify(const QString& raw) const {
    QString s = raw.toLower();
    static const QRegularExpression bad("[^a-z0-9_-]+");
    s.replace(bad, "-");                       // всё чужое — в дефисы
    static const QRegularExpression edges("^-+|-+$");
    s.replace(edges, "");                      // дефисы по краям — долой
    return s.left(32);
}

// Список с сервера целиком. Текущую комнату стараемся сохранить: человек
// открыл настройки второй комнаты, а фоновый опрос раз в десять секунд не
// должен перебросить его на первую.
void PersonalRoomController::applyRooms(const QJsonArray& rooms, int max) {
    m_rooms.clear();
    for (const QJsonValue& v : rooms)
        m_rooms.append(v.toObject().toVariantMap());
    m_maxRooms = max > 0 ? max : 1;
    m_loaded = true;
    setCurrent(m_currentId);
    emit roomChanged();
}

void PersonalRoomController::setCurrent(int roomId) {
    // Ищем запрошенную; нет такой — берём ту, где сейчас люди, иначе первую.
    // «Где люди» важнее порядка создания: обычно дело именно в ней.
    QVariantMap chosen;
    for (const QVariant& v : m_rooms) {
        const QVariantMap r = v.toMap();
        if (roomId >= 0 && r.value("id").toInt() == roomId) { chosen = r; break; }
    }
    if (chosen.isEmpty()) {
        for (const QVariant& v : m_rooms) {
            const QVariantMap r = v.toMap();
            if (r.value("online").toBool()) { chosen = r; break; }
        }
    }
    if (chosen.isEmpty() && !m_rooms.isEmpty())
        chosen = m_rooms.first().toMap();

    m_room = chosen;
    m_currentId = chosen.isEmpty() ? -1 : chosen.value("id").toInt();
}

void PersonalRoomController::select(int roomId) {
    if (roomId == m_currentId) return;
    setCurrent(roomId);
    emit roomChanged();
}

QString PersonalRoomController::roomPath(const QString& tail) const {
    return "/api/me/rooms/" + QString::number(m_currentId) + tail;
}

void PersonalRoomController::refresh() {
    QNetworkReply* reply = m_api->get("/api/me/rooms");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

        if (status == 200) {
            applyRooms(obj.value("rooms").toArray(), obj.value("max").toInt());
            return;
        }
        // Сеть/прочее: состояние НЕ трогаем — карточка не должна пропадать
        // из-за одного сорвавшегося опроса. Следующий тик таймера попробует снова.
        });
}

void PersonalRoomController::create(const QString& code, const QString& title,
                                    const QString& password) {
    const QString c = slugify(code);
    if (c.size() < 3) { setError(errText("invalid_code")); return; }   // как веб, до сервера
    setError("");
    setBusy(true);

    const QJsonObject body{ {"code", c}, {"title", title}, {"password", password} };
    QNetworkReply* reply = m_api->post("/api/me/rooms", body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 200 && obj.contains("room")) {
            // Новая комната становится текущей: человек только что её завёл,
            // показывать ему вместо неё старую — значит сделать вид, что
            // ничего не произошло.
            m_currentId = obj["room"].toObject().value("id").toInt();
            emit created();
            refresh();
            return;
        }
        if (status == 0) setError("Сервер недоступен. Попробуйте позже.");
        else             setError(errText(obj.value("error").toString()));
        });
}

// patch приходит из QML объектом: MyRoom.change({ title: "..." }).
// В теле окажутся ТОЛЬКО присланные ключи — PATCH меняет подмножество.
// Важно: password:"" — это команда «снять пароль», поэтому лишнего не шлём.
void PersonalRoomController::change(const QVariantMap& patch) {
    if (patch.isEmpty()) return;
    setError("");
    setBusy(true);

    QNetworkReply* reply = m_api->patch(roomPath(), QJsonObject::fromVariantMap(patch));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 200 && obj.contains("room")) {
            emit saved();
            refresh();
            return;
        }
        if (status == 0) setError("Сервер недоступен. Попробуйте позже.");
        else             setError(errText(obj.value("error").toString()));
        });
}

void PersonalRoomController::remove() {
    setError("");
    setBusy(true);
    QNetworkReply* reply = m_api->del(roomPath());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200 || status == 404) {   // 404 — уже удалена, тоже успех
            // Текущей станет соседняя (её выберет applyRooms) — или никакой,
            // если это была последняя.
            m_currentId = -1;
            emit removed();
            refresh();
            return;
        }
        setError(status == 0 ? "Сервер недоступен. Попробуйте позже."
                             : "Не получилось удалить. Попробуйте позже.");
        });
}

void PersonalRoomController::closeRoom() {
    QNetworkReply* reply = m_api->post(roomPath("/close"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        // Отключения выгнанных участников доезжают до сервера чуть позже самого
        // close — обновляемся с паузой, иначе увидим «в эфире» с призраками.
        // (Веб ждёт те же 500 мс.)
        QTimer::singleShot(500, this, [this]() { refresh(); });
        });
}

// Выход из аккаунта: комната следующего пользователя не должна мигнуть чужой.
void PersonalRoomController::reset() {
    m_loaded = false;
    m_room.clear();
    m_rooms.clear();
    m_currentId = -1;
    setError("");
    emit roomChanged();
    m_aliases.clear();
    m_aliasesLoaded = false;
    setAliasError("");
    emit aliasesChanged();
}

void PersonalRoomController::loadAliases() {
    setAliasError("");
    QNetworkReply* reply = m_api->get(roomPath("/aliases"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_aliases.clear();
        for (const QJsonValue& v : obj.value("aliases").toArray())
            m_aliases.append(v.toObject().toVariantMap());
        m_aliasesLoaded = true;      // даже пустой ответ — это «загрузились»
        emit aliasesChanged();
        });
}

// Поля приходят сырыми строками из формы — парсим здесь, как AliasCreateForm веба.
void PersonalRoomController::createAlias(const QString& password, const QString& usesText,
                                         const QString& loginsText) {
    QJsonObject body;
    body["password"] = password;                     // пусто = без пароля

    const QString uses = usesText.trimmed();
    if (!uses.isEmpty()) {                           // пусто = без лимита
        bool ok = false;
        const int n = uses.toInt(&ok);
        if (!ok || n < 1) { setAliasError(aliasErrText("invalid_uses")); return; }
        body["uses"] = n;
    }
    // «ivan, anna» / «ivan anna» -> ["ivan","anna"]; пусто = пускать всех.
    static const QRegularExpression sep("[,\\s]+");
    const QStringList logins = loginsText.split(sep, Qt::SkipEmptyParts);
    if (!logins.isEmpty()) body["logins"] = QJsonArray::fromStringList(logins);

    setAliasError("");
    setBusy(true);
    QNetworkReply* reply = m_api->post(roomPath("/aliases"), body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 200 && obj.contains("alias")) {
            m_aliases.append(obj["alias"].toObject().toVariantMap());
            emit aliasesChanged();
            emit aliasCreated();
            return;
        }
        if (status == 0) setAliasError("Сервер недоступен. Попробуйте позже.");
        else             setAliasError(aliasErrText(obj.value("error").toString()));
        });
}

void PersonalRoomController::toggleAlias(int id, bool enabled) {
    QNetworkReply* reply = m_api->patch(roomPath("/aliases/" + QString::number(id)),
                                        QJsonObject{ {"enabled", enabled} });
    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 200 && obj.contains("alias")) {
            // Точечная замена по id — сервер вернул алиас целиком.
            for (int i = 0; i < m_aliases.size(); ++i)
                if (m_aliases[i].toMap().value("id").toInt() == id) {
                    m_aliases[i] = obj["alias"].toObject().toVariantMap();
                    break;
                }
            emit aliasesChanged();
            return;
        }
        setAliasError(aliasErrText(obj.value("error").toString()));
        });
}

void PersonalRoomController::deleteAlias(int id) {
    QNetworkReply* reply = m_api->del(roomPath("/aliases/" + QString::number(id)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200 || status == 404) {        // 404 — уже удалён, тоже успех
            for (int i = 0; i < m_aliases.size(); ++i)
                if (m_aliases[i].toMap().value("id").toInt() == id) {
                    m_aliases.removeAt(i);
                    break;
                }
            emit aliasesChanged();
            setAliasError("");
        }
        });
}
