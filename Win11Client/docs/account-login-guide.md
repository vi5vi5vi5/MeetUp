# Инструкция №1 — Вход в аккаунт (HTTP + сессия)

Это первая инструкция из серии «взаимодействие с сервером». Дизайн экранов
`login / register / anon_lobby / home` уже готов и работает на мок-данных
(`qml/*.qml`). Здесь мы **оживляем вход**: кнопка «Войти» реально идёт на сервер,
проверяет логин/пароль, показывает ошибку при неверном пароле и — при успехе —
переводит на главную (`HomeScreen`). Плюс авто-вход, если сессия ещё жива.

Цель — чтобы ты **написал код сам и понимал каждую строчку**. Поэтому здесь:
что за классы Qt, зачем они, какие у их функций аргументы, что в них передавать и
откуда брать значения; как разбить на файлы; как показать ошибку; как перейти на
следующий экран. В конце — тестирование и подводные камни.

Эталон, с которым сверяемся (веб-клиент делает ровно это):
- `Server/web/login.html` — экран входа (функция `submit()`).
- `Server/web/assets/meetup-common.js` — обёртки `api()`, `authLogin()`, `authMe()`.
- Методичка сервера `Server/docs/desktop-client-guide.md`, разделы 3.1–3.2 (аккаунты и сессия).

---

## 1. Что происходит при входе — полный поток

Сначала пойми сценарий целиком, потом будем писать код под каждый шаг.

```
Пользователь вводит логин + пароль, жмёт «Войти»
        │
        ▼
[Клиент] POST https://meetup.linkpc.net/api/auth/login
         тело: {"login":"anna","password":"secret"}   (JSON)
        │
        ▼
[Сервер] проверяет пару логин/пароль (PBKDF2, ~0.1–0.3 с — это намеренно)
        │
   ┌────┴─────────────────────────────┐
   ▼ верно                            ▼ неверно
 200 + {"user": {...}}              401 + {"error":"wrong_credentials"}
 + заголовок Set-Cookie:              (куки нет)
   meetup_session=<токен>
   HttpOnly; Max-Age=2592000
        │                                  │
        ▼                                  ▼
 сохраняем имя, идём на HomeScreen   показываем «Неверный логин или пароль.»
```

Ключевые факты сервера (из методички §3.1–3.2):

- Вход — это `POST /api/auth/login` с телом `{"login": ..., "password": ...}`.
- Успех: код **200**, тело `{"user": {"id":1,"login":"anna","display_name":"Анна","avatar_ver":0}}`,
  и **кука `meetup_session`** в заголовке ответа.
- Неверная пара: код **401**, тело `{"error":"wrong_credentials"}`.
- Кука `meetup_session` — **HttpOnly** (JS/клиент её «не читает», но браузер/Qt
  хранят и шлют автоматически). Живёт 30 дней. По ней потом ходят все
  приватные запросы (`GET /api/me`, `/api/me/room`, …) и даже WebSocket.
- Значит: **один и тот же HTTP-клиент** должен делать и вход, и последующие
  запросы, иначе кука «потеряется» и сервер ответит `no_session`.

Отдельный, но важный кусок того же сценария — **авто-вход**: при старте
приложения мы спрашиваем `GET /api/me`. Если сессия ещё жива, сервер вернёт
`200 + {"user":...}` — и мы сразу показываем `HomeScreen`, минуя ввод пароля
(в вебе это `useEffect(() => authMe()...)` в начале `LoginScreen`).

---

## 2. Какие инструменты Qt нам нужны и зачем

Весь вход — это HTTP-запросы + разбор JSON + мостик C++↔QML. Всё есть в модулях
**Qt Network** и **Qt Qml**. Ниже — что за классы и почему именно они.

| Класс / механизм | Модуль | Зачем он нам |
|---|---|---|
| `QNetworkAccessManager` | Network | «Движок» HTTP. Через него шлём GET/POST. Живёт долго (один на всё приложение), внутри держит пул соединений и cookie jar. |
| `QNetworkRequest` | Network | Описание одного запроса: URL + заголовки (`Content-Type`, `Accept`). |
| `QNetworkReply` | Network | Асинхронный «ответ». Это `QIODevice` + сигналы `finished()` / `errorOccurred()`. Из него читаем статус-код и тело. |
| `QNetworkCookieJar` | Network | Хранилище кук. Автоматически запоминает `Set-Cookie` из ответа и подставляет `Cookie` в следующие запросы — так «держится» сессия. |
| `QJsonObject`, `QJsonDocument` | Core | Собрать тело запроса (`{"login":...}`) и разобрать тело ответа (`user` / `error`). |
| `QUrl` | Core | Тип URL для `QNetworkRequest`. |
| `QObject` + `Q_OBJECT` | Core | База для нашего C++-класса, который будет виден из QML: свойства, методы, сигналы. |
| `Q_PROPERTY` | Core | Свойство, за которым QML следит (например `errorText`, `busy`) — QML сам перерисуется при изменении. |
| `Q_INVOKABLE` | Core | Помечает метод C++, который **можно вызвать из QML** (например `Auth.login(...)`). |
| `signal` (Qt-сигнал) | Core | Событие C++ → QML (например `loggedIn()`). QML ловит через `Connections`. |
| `rootContext()->setContextProperty` | Qml | Простой способ «положить» C++-объект в QML под именем `Auth`. |

Почему асинхронно (через сигналы), а не «дождаться ответа и вернуть результат»?
Потому что вход занимает 0.1–0.3 с, и если ждать в главном потоке — окно
**замёрзнет**. Поэтому мы шлём запрос и просто подписываемся на его `finished()`;
UI в это время живёт (можно показать «busy»), а когда ответ придёт — обработаем.

---

## 3. Как разбить код на файлы

Добавляем два C++-класса и правим три существующих файла. Разделяем «транспорт»
(как ходить по HTTP) и «логику входа» (что значит успех/ошибка) — это чистая
архитектура и пригодится дальше (регистрация, комнаты — тот же транспорт).

```
Win11Client/
  src/
    main.cpp                 (правим: создаём объекты, отдаём их в QML)
    net/
      ApiClient.h  ApiClient.cpp     (НОВОЕ — транспорт: QNAM + куки + get/post)
      AuthController.h AuthController.cpp  (НОВОЕ — логика входа, видна из QML как Auth)
  qml/
    LoginScreen.qml          (правим: кнопки зовут Auth.login, показываем ошибку)
    Main.qml                 (правим: на сигнал Auth.loggedIn -> replace(homePage))
  CMakeLists.txt             (правим: добавляем Qt6::Network и новые исходники)
```

- **`ApiClient`** — «тонкий» слой над `QNetworkAccessManager`. Не знает про
  «логин/пароль», умеет только `get(path)` и `post(path, json)` и держит **одну**
  cookie jar на всё приложение. Его переиспользуют все будущие запросы.
- **`AuthController`** — то, что видит QML под именем `Auth`. Хранит состояние
  входа (`busy`, `errorText`, `displayName`), метод `login()`, метод
  `checkSession()`, сигнал `loggedIn()`. Внутри пользуется `ApiClient`.

Почему два класса, а не один: если свалить всё в одну кучу, при добавлении
регистрации/комнат придётся копировать код HTTP. А так — `ApiClient` один,
контроллеров может быть много (`AuthController`, потом `RoomController` и т.д.).

---

## 4. Шаг 0 — подключаем Qt Network в сборке

Открой `CMakeLists.txt`. Найди строку `find_package(Qt6 ... COMPONENTS ...)` и
добавь `Network`. Затем добавь новые `.cpp` в `qt_add_executable` и слинкуй
`Qt6::Network`.

```cmake
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network)

qt_add_executable(Win11Client
    src/main.cpp
    src/net/ApiClient.cpp        # + новый
    src/net/AuthController.cpp   # + новый
)

target_link_libraries(Win11Client PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    Qt6::Network                 # + новый
)
```

Заголовки (`.h`) в CMake перечислять не нужно — их подтянет `#include`. После
правки один раз сделай `scripts\configure.ps1 -Debug` (CMake перечитает файл).

> Проверка предпосылки: модуль Network есть в ките (мы уже видели `Qt6Network.dll`
> в сборке — его тянет QtQml). Так что доустанавливать ничего не надо.

---

## 5. Шаг 1 — `ApiClient` (транспорт)

### 5.1. Заголовок `src/net/ApiClient.h`

```cpp
#pragma once
#include <QObject>
#include <QNetworkAccessManager>

class QNetworkReply;

// Тонкая обёртка над QNetworkAccessManager: единый HTTP-клиент приложения
// с одной cookie jar (в ней живёт сессия meetup_session).
class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);

    // GET <base>/<path>. Возвращает "ответ" — на его finished() подпишется вызвавший.
    QNetworkReply *get(const QString &path);

    // POST <base>/<path> с телом-объектом JSON (может быть пустым).
    QNetworkReply *post(const QString &path, const QJsonObject &body = {});

private:
    QNetworkRequest makeRequest(const QString &path) const;

    QNetworkAccessManager m_nam;          // «движок»; живёт вместе с ApiClient
    QString m_base = "https://meetup.linkpc.net";  // прод-сервер (Let's Encrypt)
};
```

Пояснения:

- `QNetworkAccessManager m_nam;` — **поле-член**, а не создаётся на каждый запрос.
  Это критично: cookie jar живёт внутри него, значит кука от `login` останется в
  этом же `m_nam` и уедет в `GET /api/me`. Один `ApiClient` = один `m_nam` = одна
  сессия.
- `m_base` — база URL. Прод — `https://meetup.linkpc.net` (валидный
  Let's Encrypt-сертификат, поэтому TLS Qt проверит сам, без плясок с
  `sslErrors`). Для локального сервера поставил бы `http://localhost` (порт 80).
  Позже вынесем в настройку; пока хватит константы.
- Методы возвращают `QNetworkReply *` — это «незавершённый ответ». Тот, кто
  вызвал `post()`, сам подпишется на его `finished()` и разберёт результат. Так
  `ApiClient` остаётся общим и ничего не знает про логин.

### 5.2. Реализация `src/net/ApiClient.cpp`

```cpp
#include "ApiClient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

ApiClient::ApiClient(QObject *parent) : QObject(parent) {
    // QNetworkAccessManager по умолчанию УЖЕ имеет cookie jar (QNetworkCookieJar)
    // и сам хранит/шлёт куки в пределах жизни m_nam. Отдельно создавать не нужно.
    // (Куки живут только в памяти — при перезапуске приложения сессия «забудется».
    //  Как сделать её постоянной — в разделе «Подводные камни».)
}

// Собираем QNetworkRequest: URL + заголовки, общие для всех запросов.
QNetworkRequest ApiClient::makeRequest(const QString &path) const {
    QNetworkRequest req{ QUrl(m_base + path) };
    // Просим сервер отвечать JSON-ом (как это делает web: header "Accept").
    req.setRawHeader("Accept", "application/json");
    return req;
}

QNetworkReply *ApiClient::get(const QString &path) {
    return m_nam.get(makeRequest(path));
}

QNetworkReply *ApiClient::post(const QString &path, const QJsonObject &body) {
    QNetworkRequest req = makeRequest(path);
    // Тело — JSON. Обязательно сказать серверу тип содержимого.
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // QJsonObject -> компактный QByteArray (без пробелов/переносов).
    const QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return m_nam.post(req, data);
}
```

Что за функции и аргументы:

- **`QNetworkRequest req{ QUrl(...) }`** — конструктор запроса из URL. `QUrl`
  просто оборачивает строку адреса.
- **`req.setRawHeader(name, value)`** — задать «сырой» HTTP-заголовок байтами.
  `name = "Accept"`, `value = "application/json"`. Это подсказка серверу отдавать
  JSON.
- **`req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json")`** —
  типизированный способ выставить `Content-Type`. Первый аргумент —
  enum «какой известный заголовок», второй — значение. Нужен только для POST
  (у тела есть тип); у GET тела нет.
- **`QJsonDocument(body).toJson(QJsonDocument::Compact)`** — превращает объект
  `{"login": "...", "password": "..."}` в байты `{"login":"...","password":"..."}`.
  `Compact` — без форматирования (для сети — то, что надо).
- **`m_nam.post(req, data)`** — сигнатура
  `QNetworkReply *post(const QNetworkRequest &request, const QByteArray &data)`.
  Первый аргумент — что и куда (URL+заголовки), второй — тело. Возвращает
  `QNetworkReply *` **сразу** (запрос ещё летит!). Ответ придёт позже — по сигналу
  `finished()`.
- **`m_nam.get(req)`** — то же, но без тела (`QNetworkReply *get(const QNetworkRequest&)`).

---

## 6. Шаг 2 — `AuthController` (логика входа, виден из QML)

Это «мозг» входа и одновременно API для QML. Он держит состояние и разбирает
ответы `ApiClient`.

### 6.1. Заголовок `src/net/AuthController.h`

```cpp
#pragma once
#include <QObject>
#include <QString>

class ApiClient;

class AuthController : public QObject {
    Q_OBJECT
    // Свойства, за которыми следит QML. NOTIFY-сигнал => QML сам обновится.
    Q_PROPERTY(bool    busy        READ busy        NOTIFY busyChanged)
    Q_PROPERTY(QString errorText   READ errorText   NOTIFY errorTextChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY displayNameChanged)
public:
    explicit AuthController(ApiClient *api, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString errorText() const { return m_errorText; }
    QString displayName() const { return m_displayName; }

    // Вызывается из QML: Auth.login("anna", "secret")
    Q_INVOKABLE void login(const QString &login, const QString &password);
    // Вызывается один раз при старте: авто-вход по живой сессии.
    Q_INVOKABLE void checkSession();

signals:
    void busyChanged();
    void errorTextChanged();
    void displayNameChanged();
    void loggedIn();          // успех: QML переходит на HomeScreen

private:
    void setBusy(bool v);
    void setError(const QString &text);

    ApiClient *m_api;         // не владеем (передан снаружи)
    bool m_busy = false;
    QString m_errorText;
    QString m_displayName;
};
```

Пояснения:

- **`Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)`** — объявляет свойство
  `busy`. `READ busy` — геттер, `NOTIFY busyChanged` — сигнал, который надо
  испустить при изменении, чтобы QML перечитал. В QML это `Auth.busy`.
- **`Q_INVOKABLE void login(...)`** — метод, который **разрешено звать из QML**.
  Без `Q_INVOKABLE` QML метод не увидит.
- **`signals: void loggedIn();`** — Qt-сигнал. Мы его испустим при успехе; QML
  подпишется и сделает переход.
- `ApiClient *m_api` — контроллер **пользуется** транспортом, но не владеет им
  (создадим оба в `main.cpp`, где и решим порядок удаления).

### 6.2. Реализация `src/net/AuthController.cpp`

```cpp
#include "AuthController.h"
#include "ApiClient.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

AuthController::AuthController(ApiClient *api, QObject *parent)
    : QObject(parent), m_api(api) {}

void AuthController::setBusy(bool v) {
    if (m_busy == v) return;
    m_busy = v;
    emit busyChanged();           // сообщаем QML: значение поменялось
}
void AuthController::setError(const QString &text) {
    if (m_errorText == text) return;
    m_errorText = text;
    emit errorTextChanged();
}

void AuthController::login(const QString &login, const QString &password) {
    // 1) Клиентская валидация — не бьём сервер зря (как web: "Введите логин и пароль").
    if (login.trimmed().isEmpty() || password.isEmpty()) {
        setError("Введите логин и пароль.");
        return;
    }
    setError("");                 // стираем прошлую ошибку
    setBusy(true);                // кнопка «Войти» станет неактивной (см. QML)

    // 2) Тело запроса — тот же JSON, что шлёт web-клиент.
    QJsonObject body{ {"login", login.trimmed()}, {"password", password} };
    QNetworkReply *reply = m_api->post("/api/auth/login", body);

    // 3) Подписываемся на ЗАВЕРШЕНИЕ запроса. Лямбда выполнится, когда придёт ответ.
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setBusy(false);
        reply->deleteLater();     // ВАЖНО: сами удаляем reply, иначе течёт память

        // HTTP-код ответа: атрибут HttpStatusCodeAttribute (QVariant -> int).
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Тело -> JSON. Может быть пустым/битым — тогда объект пустой.
        const QJsonObject obj =
            QJsonDocument::fromJson(reply->readAll()).object();

        // Успех: 200 и есть объект user.
        if (status == 200 && obj.contains("user")) {
            const QJsonObject user = obj["user"].toObject();
            m_displayName = user["display_name"].toString();
            emit displayNameChanged();
            // Кука meetup_session уже сохранилась в cookie jar внутри ApiClient.
            emit loggedIn();      // QML: переход на HomeScreen
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
    QNetworkReply *reply = m_api->get("/api/me");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject obj =
            QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 200 && obj.contains("user")) {
            m_displayName = obj["user"].toObject()["display_name"].toString();
            emit displayNameChanged();
            emit loggedIn();      // сразу на главную, пароль не спрашиваем
        }
        // Иначе (401 no_session) — молчим: пусть пользователь вводит логин.
    });
}
```

Разбор незнакомых функций:

- **`connect(reply, &QNetworkReply::finished, this, лямбда)`** — «когда `reply`
  испустит `finished()`, выполнить лямбду в контексте `this`». `finished()`
  приходит ровно один раз, когда ответ полностью получен (или упал). Контекст
  `this` (4-й аргумент) нужен, чтобы Qt сам отписал соединение, если контроллер
  удалят раньше.
- **`reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)`** — читает
  «атрибут» ответа. `HttpStatusCodeAttribute` — enum «HTTP-код». Возвращает
  `QVariant`; `.toInt()` → число (200, 401, …). Если запрос **не долетел**
  (нет сети/DNS), атрибута нет и `.toInt()` даст `0` — это наш «status 0».
- **`reply->readAll()`** — прочитать всё тело ответа как `QByteArray` (`QNetworkReply`
  — это `QIODevice`).
- **`QJsonDocument::fromJson(bytes).object()`** — распарсить байты в документ и
  взять корневой объект. Если тело не JSON — вернётся пустой `QJsonObject`
  (поэтому дальше проверяем `contains("user")`, а не падаем).
- **`obj["user"].toObject()`, `["display_name"].toString()`** — навигация по JSON:
  берём поле `user` (это вложенный объект), из него — строку `display_name`.
- **`reply->deleteLater()`** — reply создаёт `QNetworkAccessManager`, а
  **удалять его обязаны мы**. `deleteLater()` — безопасно (удалит, когда вернёмся
  в цикл событий, уже после лямбды). Забудешь — утечка на каждый вход.
- **`emit ...`** — «испустить сигнал». `emit loggedIn()` разбудит `Connections` в
  QML.

---

## 7. Шаг 3 — отдать `Auth` в QML (`main.cpp`)

Создаём оба объекта до загрузки QML и кладём контроллер в контекст под именем
`Auth`. Порядок важен: сначала регистрируем, потом `loadFromModule`.

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include "net/ApiClient.h"
#include "net/AuthController.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("MeetUp");
    app.setApplicationName("MeetUp");
    QQuickStyle::setStyle("Basic");

    // Транспорт и контроллер живут всё приложение (стек main).
    ApiClient api;
    AuthController auth(&api);

    QQmlApplicationEngine engine;
    // Кладём объект в глобальный контекст QML под именем "Auth".
    // Теперь в любом .qml доступно Auth.login(...), Auth.busy, Auth.errorText.
    engine.rootContext()->setContextProperty("Auth", &auth);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("MeetUp", "Main");
    return app.exec();
}
```

Пояснения:

- **`engine.rootContext()->setContextProperty("Auth", &auth)`** — самый простой
  способ «поделиться» C++-объектом с QML. Имя `"Auth"` станет глобально видимым в
  QML; `&auth` — указатель на наш объект. `import`-ы для этого не нужны.
- Почему `ApiClient api; AuthController auth(&api);` **до** `engine` и в `main`:
  они должны пережить весь `app.exec()`. Порядок уничтожения обратный объявлению —
  `engine` удалится первым, потом `auth`, потом `api` (это ок).

> Альтернатива (более «типизированная», на будущее): пометить `AuthController`
> макросами `QML_ELEMENT`/`QML_SINGLETON`, добавить `.cpp/.h` в `qt_add_qml_module
> (SOURCES ...)` и обращаться как к типу модуля `MeetUp`. Для первого раза
> `setContextProperty` проще и полностью рабоче — начни с него.

---

## 8. Шаг 4 — оживляем `LoginScreen.qml`

Сейчас в `LoginScreen.qml` кнопки испускают сигналы `loginRequested/…`, а
`err` — просто локальное свойство. Меняем так, чтобы:

1. «Войти» звала **`Auth.login(login, password)`**;
2. текст ошибки брался из **`Auth.errorText`**;
3. кнопки блокировались, пока идёт запрос (**`Auth.busy`**).

Правки (показаны только изменённые места):

```qml
// было: property string err: ""   — больше не нужно, берём из Auth.

// поле пароля: Enter тоже логинит
AppInput {
    id: passInput
    width: parent.width
    isPassword: true
    placeholderText: "••••••••"
    onAccepted: Auth.login(loginInput.text, passInput.text)   // <—
}

// кнопка «Войти»
AppButton {
    width: parent.width
    text: "Войти"
    variant: "primary"
    iconRight: "arrow-right"
    enabled: !Auth.busy                                        // <— пока грузит — недоступна
    onClicked: Auth.login(loginInput.text, passInput.text)    // <—
}

// строка-сообщение снизу: ошибка из Auth
Text {
    width: parent.width
    horizontalAlignment: Text.AlignHCenter
    wrapMode: Text.WordWrap
    text: Auth.errorText !== "" ? Auth.errorText
                                : "Аккаунт хранит ваше имя между встречами."
    color: Auth.errorText !== "" ? Theme.danger : Theme.textFaint   // <— красный при ошибке
    font.family: Theme.uiFont
    font.pixelSize: Theme.textXs
}
```

Как это работает без единой «ручной» перерисовки: `Auth.errorText` и `Auth.busy` —
это `Q_PROPERTY` с `NOTIFY`. Как только C++ вызовет `emit errorTextChanged()`,
QML-биндинги (`text:`, `color:`, `enabled:`) **сами** пересчитаются. Это и есть
главный кайф связки Qt/QML — описал зависимость один раз, дальше оно живёт само.

Кнопки «Создать аккаунт» и «Войти без аккаунта» пока оставь на своих сигналах
(`registerRequested` / `anonRequested`) — это следующие инструкции.

> Показ ошибки при неверном пароле — целиком здесь: сервер вернул 401 →
> `AuthController::login` вызвал `setError("Неверный логин или пароль.")` →
> `errorTextChanged` → `Text.text`/`color` обновились → пользователь видит красную
> строку. Никаких диалогов и таймеров не нужно.

---

## 9. Шаг 5 — переход на главную (`Main.qml`)

Экран не сам решает, куда идти после входа, — он лишь дергает `Auth.login()`.
А слушает результат и навигирует **`Main.qml`**, потому что именно он владеет
`StackView`. Ловим глобальный сигнал `Auth.loggedIn` через `Connections`:

```qml
StackView {
    id: stack
    anchors.fill: parent
    initialItem: loginPage
    // ... transitions ...
}

// Реакция на успешный вход (из любого места: и обычный login, и авто-вход).
Connections {
    target: Auth
    function onLoggedIn() {
        // Если мы уже на home — не дублируем.
        if (stack.currentItem && stack.currentItem.objectName === "home") return;
        stack.replace(homePage);
    }
}

// Авто-вход при старте: спрашиваем /api/me один раз.
Component.onCompleted: Auth.checkSession()
```

Чтобы `objectName === "home"` работал, добавь в корень `HomeScreen.qml`
строку `objectName: "home"` (мелочь, чтобы не перескакивать на home повторно).
Можно и без этой проверки — тогда просто всегда `stack.replace(homePage)`.

Итог по навигации:
- **ручной вход**: `Auth.login()` → (200) → `loggedIn` → `Connections` →
  `replace(homePage)`.
- **авто-вход**: `Component.onCompleted` → `Auth.checkSession()` → (жива сессия) →
  `loggedIn` → та же навигация. Пользователь даже не увидит форму.

---

## 10. Как это протестировать

Сервер `meetup.linkpc.net` уже поднят. Аккаунт для входа нужно сначала создать
(регистрацию мы ещё не оживили — сделаем в следующей инструкции), поэтому заведи
тестовый аккаунт вручную через `curl` (или временно через веб-клиент в браузере):

```powershell
# создать аккаунт (один раз)
curl.exe -X POST https://meetup.linkpc.net/api/auth/register `
  -H "Content-Type: application/json" `
  -d '{"login":"tester","password":"test12345","display_name":"Тестер"}'
```

Затем:

1. `scripts\run.ps1` — запусти клиент.
2. Введи `tester` / `test12345` → должен уйти на `HomeScreen`.
3. Введи неверный пароль → красная строка «Неверный логин или пароль.», остаёшься
   на форме.
4. Закрой и снова открой приложение → благодаря `checkSession()` и живой куке
   попадёшь на `HomeScreen` без ввода пароля. (Пока кука в памяти — см. ниже; она
   сбросится, если приложение полностью закрыть. Постоянное хранение — по желанию.)

Как «подсмотреть», что реально уходит на сервер: временно добавь в лямбды
`qDebug() << status << reply->readAll();` — увидишь код и тело в консоли
(запускай отладочную сборку из `run.ps1`).

---

## 11. Подводные камни (прочитай до того, как споткнёшься)

- **Один `ApiClient` (одна cookie jar) на всё.** Если создашь новый
  `QNetworkAccessManager` под каждый запрос — кука `meetup_session` не сохранится,
  и `GET /api/me` вернёт `no_session`. Держи `ApiClient` единым (мы так и сделали:
  один в `main`).
- **Асинхронность.** `post()`/`get()` возвращают `QNetworkReply*` мгновенно, ответа
  ещё нет. Всё, что зависит от ответа, — только внутри лямбды на `finished()`.
  Не пытайся «дождаться» ответа в строке после `post()`.
- **`reply->deleteLater()` обязателен** в каждой лямбде — иначе утечка памяти на
  каждый запрос.
- **`status == 0`** — это не «ок», это «до сервера не дошли» (нет сети, DNS,
  таймаут). Обрабатывай как «Сервер недоступен».
- **TLS.** Прод за nginx с Let's Encrypt → сертификат валидный, `QNetworkAccessManager`
  проверит его сам, ничего отключать не надо. (Если однажды поставишь
  самоподписанный — тогда понадобится обработчик `sslErrors` с пиннингом по
  отпечатку, но это не наш случай.)
- **Постоянная сессия (по желанию).** Стандартная cookie jar держит куку только в
  памяти — закрыл приложение, сессия «забылась» (при следующем старте снова
  форма). Чтобы «запомнить меня», нужно свой класс — наследник
  `QNetworkCookieJar` — который на `setCookiesFromUrl()` пишет куки в
  `QSettings`/файл, а в конструкторе читает обратно. Это отдельная маленькая
  задача; для первой версии не обязательна.
- **`no_session` в любой момент.** Сессия может истечь (30 дней) или быть
  почищена. Любой приватный запрос может вернуть 401 — не падай, а мягко верни на
  логин (это доведём, когда появятся запросы главной).
- **Не логируй пароль** в `qDebug()`. Логируй статус и, максимум, `body["error"]`.

---

## 12. Что дальше

Следующие инструкции той же серии (каждую сделаем отдельно, чтобы не раздувать):

1. **Регистрация** — `POST /api/auth/register`, те же приёмы + клиентская
   валидация логина/пароля, маппинг ошибок (`login_taken`, `weak_password`…),
   переход на `HomeScreen`. (Экран `RegisterScreen` уже готов.)
2. **Данные главной** — `GET /api/me` (профиль) и `GET /api/me/room` (личная
   комната): подставить реальные имя/аватар/комнату вместо `MockData` в
   `HomeScreen`. Здесь же — `logout` (`POST /api/auth/logout`) и выход на логин.
3. **Вход в комнату** — `POST /api/rooms` / `GET /api/rooms/<code>` (это уже без
   аккаунта, как в `anon_lobby`).
4. Дальше — WebSocket (`join`, участники, чат) — веха M1 из `ROADMAP.md`.

Когда будешь готов — скажи «делаем регистрацию» (или что-то из списка), и я
напишу следующую инструкцию в том же формате.
