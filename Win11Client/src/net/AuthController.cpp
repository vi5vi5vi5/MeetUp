#include "AuthController.h"
#include "ApiClient.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QImage>
#include <QPainter>
#include <QBuffer>
#include <QUrl>

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

// Применить объект user из любого ответа (login/register/me/PATCH/avatar).
// Все свойства профиля обновляются здесь и только здесь.
void AuthController::applyUser(const QJsonObject& user) {
    m_userId = static_cast<qint64>(user.value("id").toDouble());
    m_login = user.value("login").toString();
    m_avatarVer = user.value("avatar_ver").toInt();

    const QString dn = user.value("display_name").toString();
    if (m_displayName != dn) {
        m_displayName = dn;
        emit displayNameChanged();
    }
    emit profileChanged();   // avatarUrl/userLogin пересчитаются в QML
}

// Пусто, если аватарки нет. Версия в URL — кэш-ключ: картинка отдаётся
// «кэшировать вечно», но после замены avatar_ver растёт и URL меняется сам.
QString AuthController::avatarUrl() const {
    if (m_userId <= 0 || m_avatarVer <= 0) return {};
    return m_api->baseUrl() + "/api/users/" + QString::number(m_userId)
        + "/avatar?v=" + QString::number(m_avatarVer);
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
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();  // ВАЖНО: сами удаляем reply, иначе течёт память

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

        // Успех: 200 и есть объект user.
        if (status == 200 && obj.contains("user")) {
            applyUser(obj["user"].toObject());
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
            applyUser(obj["user"].toObject());
            emit loggedIn();      // сразу на главную, пароль не спрашиваем
        }
        // Иначе (401 no_session) — молчим: пусть пользователь вводит логин.
        });
}

void AuthController::registerAccount(const QString& login, const QString& displayName,
                                     const QString& password, const QString& password2) {
    // 1) Нормализация — как сервер: логин без пробелов и в нижнем регистре.
    const QString l = login.trimmed().toLower();
    const QString name = displayName.trimmed();

    // 2) Клиентская валидация — те же проверки и тексты, что у web (submit()).
    if (l.isEmpty() || name.isEmpty() || password.isEmpty()) {
        setError("Заполните все поля.");
        return;
    }
    static const QRegularExpression loginRx("^[a-z0-9_.-]{3,32}$");
    if (!loginRx.match(l).hasMatch()) {
        setError("Логин: 3–32 символа — латиница, цифры, точка, дефис, подчёркивание.");
        return;
    }
    if (password.size() < 8) {
        setError("Пароль слишком короткий — минимум 8 символов.");
        return;
    }
    if (password != password2) {
        setError("Пароли не совпадают.");
        return;
    }

    setError("");
    setBusy(true);

    // 3) Тело — как authRegister() в meetup-common.js (имя поля — display_name!).
    QJsonObject body{ {"login", l}, {"password", password}, {"display_name", name} };
    QNetworkReply* reply = m_api->post("/api/auth/register", body);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

        // Успех: аккаунт создан и сессия уже открыта (кука осела в jar).
        if (status == 200 && obj.contains("user")) {
            applyUser(obj["user"].toObject());
            emit loggedIn();                 // та же навигация, что при входе
            return;
        }

        // Ошибки: ветвимся по машинному коду из тела (словарь ERRORS веба).
        const QString code = obj.value("error").toString();
        if (code == "login_taken")
            setError("Такой логин уже занят. Попробуйте другой.");
        else if (code == "invalid_login")
            setError("Логин: 3–32 символа — латиница, цифры, точка, дефис, подчёркивание.");
        else if (code == "invalid_display_name")
            setError("Отображаемое имя: от 1 до 40 символов.");
        else if (code == "weak_password")
            setError("Пароль слишком короткий — минимум 8 символов.");
        else if (status == 0)
            setError("Сервер недоступен. Попробуйте позже.");
        else
            setError("Ошибка сервера (" + QString::number(status) + ").");
        });
}

void AuthController::logout() {
    QNetworkReply* reply = m_api->post("/api/auth/logout");   // тело пустое
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        // Ответ сервера почти не важен: 200 — сессия погашена и кука стёрта
        // (Set-Cookie Max-Age=0 обработает cookie jar сам). Даже если сервер
        // не ответил — локально мы всё равно выходим (как веб: await и уход).
        m_userId = 0;
        m_login.clear();
        m_avatarVer = 0;
        m_displayName.clear();
        emit displayNameChanged();
        emit profileChanged();
        setError("");
        emit loggedOut();          // Main.qml вернёт на LoginScreen
        });
}

void AuthController::updateDisplayName(const QString& name) {
    const QString n = name.trimmed();
    if (n.isEmpty()) { setError("Имя не может быть пустым."); return; }
    if (n == m_displayName) { emit profileSaved(); return; }   // менять нечего — выходим из правки

    setError("");
    setBusy(true);
    QNetworkReply* reply = m_api->patch("/api/me", QJsonObject{ {"display_name", n} });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

        if (status == 200 && obj.contains("user")) {
            applyUser(obj["user"].toObject());
            emit profileSaved();             // модалка закроет режим правки
            return;
        }
        const QString code = obj.value("error").toString();
        if (code == "invalid_display_name") setError("Отображаемое имя: от 1 до 40 символов.");
        else if (code == "no_session")      setError("Сессия истекла — войдите заново.");
        else if (status == 0)               setError("Сервер недоступен. Попробуйте позже.");
        else                                setError("Не удалось сохранить имя. Попробуйте позже.");
        });
}

void AuthController::uploadAvatar(const QUrl& file) {
    // FileDialog отдаёт URL вида file:///C:/... — превращаем в обычный путь.
    QImage src(file.toLocalFile());
    if (src.isNull()) { setError("Не удалось открыть изображение."); return; }

    // 1) Центр-кроп в квадрат и ужатие до 256x256 — как canvas-код веба.
    const int side = qMin(src.width(), src.height());
    const QImage square =
        src.copy((src.width() - side) / 2, (src.height() - side) / 2, side, side)
        .scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // 2) Прозрачность (PNG) -> белый фон: JPEG альфа-канала не умеет.
    QImage canvas(256, 256, QImage::Format_RGB32);
    canvas.fill(Qt::white);
    {
        QPainter p(&canvas);
        p.drawImage(0, 0, square);
    }   // painter должен разрушиться ДО save() — поэтому фигурные скобки

    // 3) Кодируем в JPEG (качество 85) прямо в память.
    QByteArray jpeg;
    QBuffer buf(&jpeg);
    buf.open(QIODevice::WriteOnly);
    canvas.save(&buf, "JPEG", 85);

    // 4) POST с base64 (обычный, не base64url!) — сервер проверит сигнатуру FF D8 FF.
    setError("");
    setBusy(true);
    QNetworkReply* reply = m_api->post("/api/me/avatar",
        QJsonObject{ {"image", QString::fromLatin1(jpeg.toBase64())} });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 200 && obj.contains("user")) {
            applyUser(obj["user"].toObject());   // avatar_ver вырос -> URL сменился
            return;
        }
        const QString code = obj.value("error").toString();
        if (code == "bad_image") setError("Не удалось загрузить фото. Попробуйте другой файл.");
        else if (status == 0)    setError("Сервер недоступен. Попробуйте позже.");
        else                     setError("Не удалось загрузить фото (" + QString::number(status) + ").");
        });
}

void AuthController::removeAvatar() {
    setError("");
    setBusy(true);
    QNetworkReply* reply = m_api->del("/api/me/avatar");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 200 && obj.contains("user"))
            applyUser(obj["user"].toObject());   // avatar_ver стал 0 -> снова инициалы
        // Ошибку молча глотаем: удалять уже нечего — цель и так достигнута.
        });
}
