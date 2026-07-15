#include "AuthController.h"
#include "ApiClient.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

AuthController::AuthController(ApiClient* api, QObject* parent)
    : QObject(parent), m_api(api) {}

void AuthController::setBusy(bool v) {
    if (m_busy == v) return;
    m_busy = v;
    emit busyChanged();  // сообщаем QML: значение поменялось
}
void AuthController::setError(const QString& text) {
    if (m_errorText == text) return;
    m_errorText = text;
    emit errorTextChanged();
}

void AuthController::login(const QString& login, const QString& password) {
    // 1) Клиентская валидация — не бьём сервер зря (как web: "Введите логин и пароль").
    if (login.trimmed().isEmpty() || password.isEmpty()) {
        setError("Введите логин и пароль.");
        return;
    }
    setError("");                 // стираем прошлую ошибку
    setBusy(true);                // кнопка «Войти» станет неактивной (см. QML)

    // 2) Тело запроса — тот же JSON, что шлёт web-клиент.
    QJsonObject body{ {"login", login.trimmed()}, {"password", password} };
    QNetworkReply* reply = m_api->post("/api/auth/login", body);

    // 3) Подписываемся на ЗАВЕРШЕНИЕ запроса. Лямбда выполнится, когда придёт ответ.
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {  // Пеоредаём в connect lambda-функцию что делать когда ответ придет
        setBusy(false);
        reply->deleteLater();  // ВАЖНО: сами удаляем reply, иначе течёт память

        // HTTP-код ответа: атрибут HttpStatusCodeAttribute (QVariant -> int).
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // Тело -> JSON. Может быть пустым/битым — тогда объект пустой.
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

        // Успех: 200 и есть объект user.
        if (status == 200 && obj.contains("user")) {
            const QJsonObject user = obj["user"].toObject();
            m_displayName = user["display_name"].toString();
            emit displayNameChanged();
            // Кука meetup_session уже сохранилась в cookie jar внутри ApiClient.
            emit loggedIn();
            return;
        }

        // Ошибки — маппим код на человеческий текст (как ERRORS в web).
        if (status == 401)      setError("Неверный логин или пароль.");
        else if (status == 0)   setError("Сервер недоступен. Попробуйте позже.");
        else                    setError("Ошибка сервера (" + QString::number(status) + ").");
        });
}

void AuthController::checkSession() {
    // Авто-вход: спрашиваем «кто я». Если сессия жива — 200 + user.
    QNetworkReply* reply = m_api->get("/api/me");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 200 && obj.contains("user")) {
            m_displayName = obj["user"].toObject()["display_name"].toString();
            emit displayNameChanged();
            emit loggedIn();      // сразу на главную, пароль не спрашиваем
        }
        // Иначе (401 no_session) — молчим: пусть пользователь вводит логин.
        });
}