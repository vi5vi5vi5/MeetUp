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
    if (code == "alias_limit")      return "Не больше 5 ссылок на комнату. Удалите одну из существующих.";
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

void PersonalRoomController::applyRoom(const QJsonObject& room) {
    m_room = room.toVariantMap();   // вложенный participant_names станет QVariantList
    m_exists = true;
    m_loaded = true;
    emit roomChanged();
}

void PersonalRoomController::refresh() {
    QNetworkReply* reply = m_api->get("/api/me/room");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

        if (status == 200 && obj.contains("room")) {
            applyRoom(obj["room"].toObject());
            return;
        }
        if (status == 404) {                    // no_room: комнаты просто нет
            m_loaded = true;
            m_exists = false;
            m_room.clear();
            emit roomChanged();
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
    QNetworkReply* reply = m_api->post("/api/me/room", body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 200 && obj.contains("room")) {
            applyRoom(obj["room"].toObject());
            emit created();
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

    QNetworkReply* reply = m_api->patch("/api/me/room", QJsonObject::fromVariantMap(patch));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 200 && obj.contains("room")) {
            applyRoom(obj["room"].toObject());
            emit saved();
            return;
        }
        if (status == 0) setError("Сервер недоступен. Попробуйте позже.");
        else             setError(errText(obj.value("error").toString()));
        });
}

void PersonalRoomController::remove() {
    setError("");
    setBusy(true);
    QNetworkReply* reply = m_api->del("/api/me/room");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200 || status == 404) {   // 404 — уже удалена, тоже успех
            m_exists = false;
            m_room.clear();
            emit roomChanged();
            emit removed();
            return;
        }
        setError(status == 0 ? "Сервер недоступен. Попробуйте позже."
                             : "Не получилось удалить. Попробуйте позже.");
        });
}

void PersonalRoomController::closeRoom() {
    QNetworkReply* reply = m_api->post("/api/me/room/close");
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
    m_exists = false;
    m_room.clear();
    setError("");
    emit roomChanged();
    m_aliases.clear();
    m_aliasesLoaded = false;
    setAliasError("");
    emit aliasesChanged();
}

void PersonalRoomController::loadAliases() {
    setAliasError("");
    QNetworkReply* reply = m_api->get("/api/me/room/aliases");
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
    QNetworkReply* reply = m_api->post("/api/me/room/aliases", body);
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
    QNetworkReply* reply = m_api->patch("/api/me/room/aliases/" + QString::number(id),
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
    QNetworkReply* reply = m_api->del("/api/me/room/aliases/" + QString::number(id));
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
