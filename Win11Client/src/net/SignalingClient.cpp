#include "SignalingClient.h"
#include "ApiClient.h"
#include <QWebSocket>
#include <QTimer>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDateTime>

SignalingClient::SignalingClient(ApiClient* api, QObject* parent)
    : QObject(parent), m_api(api) {
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_manualClose && !m_fatal) openSocket();
        });
    m_waitTimer = new QTimer(this);
    m_waitTimer->setSingleShot(true);
    connect(m_waitTimer, &QTimer::timeout, this, [this]() { sendJoin(); });
}

SignalingClient::~SignalingClient() { /* m_ws — child, удалится сам */ }

void SignalingClient::open(const QString& roomCode, const QString& name) {
    // Начинаем новую сессию входа: сбрасываем флаги прошлой.
    m_roomCode = roomCode;
    m_name = name;
    m_roomPass.clear();
    m_myId = 0;
    m_attempts = 0;
    m_manualClose = false;
    m_fatal = false;
    m_joinedOnce = false;
    m_participants.clear();
    emit participantsChanged();
    // Остатки прошлой конференции: заголовок личной комнаты и флаг реконнекта.
    if (!m_roomTitle.isEmpty()) { m_roomTitle.clear(); emit roomTitleChanged(); }
    if (m_reconnecting) { m_reconnecting = false; emit reconnectingChanged(); }
    setError("");
    setPhase("connecting");
    openSocket();
}

void SignalingClient::openSocket() {
    // Пересоздаём сокет на каждую попытку (переиспользовать закрытый нельзя).
    if (m_ws) { m_ws->deleteLater(); m_ws = nullptr; }
    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    connect(m_ws, &QWebSocket::connected, this, &SignalingClient::onConnected);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &SignalingClient::onTextMessage);
    connect(m_ws, &QWebSocket::disconnected, this, &SignalingClient::onDisconnected);
    // Диагностика: молча падающий сокет выглядит как вечное «переподключение»,
    // а причина (DNS, TLS, отказ сервера) видна только здесь.
    connect(m_ws, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
        qWarning() << "SignalingClient: ошибка сокета:" << e << m_ws->errorString();
        });
    connect(m_ws, &QWebSocket::sslErrors, this, [](const QList<QSslError>& errors) {
        for (const QSslError& e : errors)
            qWarning() << "SignalingClient: TLS:" << e.errorString();
        });

    // Хендшейк-запрос: URL + (для аккаунта) кука сессии.
    QNetworkRequest req{ QUrl(m_api->wsUrl()) };
    const QString token = m_api->sessionToken();
    if (!token.isEmpty())
        req.setRawHeader("Cookie", QByteArray("meetup_session=") + token.toUtf8());
    m_ws->open(req);
}

void SignalingClient::onConnected() {
    m_reconnecting = false;
    emit reconnectingChanged();
    sendJoin();
}

// Первое сообщение в комнату — представляемся.
void SignalingClient::sendJoin() {
    if (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState) { openSocket(); return; }
    const QJsonObject join{
        {"type", "join"},
        {"room", m_roomCode},
        {"name", m_name},           // у аккаунта сервер проигнорирует, возьмёт из профиля
        {"password", m_roomPass},   // нужен только гостю личной комнаты с паролем
    };
    m_ws->sendTextMessage(QString::fromUtf8(
        QJsonDocument(join).toJson(QJsonDocument::Compact)));
}
void SignalingClient::onTextMessage(const QString& text) {
    const QJsonObject msg =
        QJsonDocument::fromJson(text.toUtf8()).object();
    onJson(msg);
    
}

void SignalingClient::onJson(const QJsonObject& msg) {
    const QString type = msg.value("type").toString();

    if (type == "join_ok") {
        m_myId = static_cast<qint64>(msg.value("sender_id").toDouble()); // >2^31 у аккаунта
        m_attempts = 0;
        if (m_waitTimer->isActive()) m_waitTimer->stop();
        const QString title = msg.value("room_title").toString();
        if (!title.isEmpty()) { m_roomTitle = title; emit roomTitleChanged(); }

        rebuildParticipants(msg.value("participants").toArray());
        setError("");
        setPhase("live");
        // Разослать своё состояние микрофона/камеры (как делает веб после join_ok).
        setLocalState(m_mic, m_cam);
        if (!m_joinedOnce) {          // реконнект историю не задваивает (как веб)
            m_joinedOnce = true;
            emit joinedRoom(m_roomCode, title);   // title пуст у разовых комнат
        }
        // История чата: пересобираем ленту из join_ok (и на реконнекте — тоже,
        // чтобы не задвоить уже показанные сообщения).
        m_messages.clear();
        const QJsonArray history = msg.value("history").toArray();
        for (const QJsonValue& v : history) {
            const QJsonObject h = v.toObject();
            m_messages.append(makeMessage(
                static_cast<qint64>(h.value("sender_id").toDouble()),
                h.value("sender_name").toString(),
                h.value("text").toString(),
                static_cast<qint64>(h.value("timestamp_ms").toDouble())));
        }
        emit messagesChanged();
        return;
    }

    if (type == "participant_joined") {
        QVariantMap p;
        p["id"] = static_cast<qint64>(msg.value("id").toDouble());
        p["name"] = msg.value("name").toString();
        p["mic"] = msg.value("mic").toBool(true);
        p["cam"] = msg.value("cam").toBool(true);
        p["isSelf"] = false;
        p["speaking"] = false;
        // не дублируем, если такой id уже есть
        for (const QVariant& v : m_participants)
            if (v.toMap().value("id").toLongLong() == p["id"].toLongLong()) return;
        m_participants.append(p);
        emit participantsChanged();
        return;
    }

    if (type == "participant_left") {
        const qint64 id = static_cast<qint64>(msg.value("id").toDouble());
        for (int i = 0; i < m_participants.size(); ++i)
            if (m_participants[i].toMap().value("id").toLongLong() == id) {
                m_participants.removeAt(i);
                emit participantsChanged();
                break;
            }
        return;
    }

    if (type == "participant_state") {
        const qint64 id = static_cast<qint64>(msg.value("id").toDouble());
        for (int i = 0; i < m_participants.size(); ++i) {
            QVariantMap p = m_participants[i].toMap();
            if (p.value("id").toLongLong() == id) {
                p["mic"] = msg.value("mic").toBool();
                p["cam"] = msg.value("cam").toBool();
                m_participants[i] = p;
                emit participantsChanged();
                break;
            }
        }
        return;
    }

    if (type == "chat") {
        m_messages.append(makeMessage(
            static_cast<qint64>(msg.value("sender_id").toDouble()),
            msg.value("sender_name").toString(),
            msg.value("text").toString(),
            static_cast<qint64>(msg.value("timestamp_ms").toDouble())));
        emit messagesChanged();
        return;
    }

    if (type == "error") {
        handleError(msg.value("reason").toString());
        return;
    }
}

// TODO: ping и прочее, бинарные данные

void SignalingClient::rebuildParticipants(const QJsonArray& serverList) {
    m_participants.clear();
    // 0) сам пользователь — первым, как в мок-данных.
    QVariantMap me;
    me["id"] = m_myId;
    me["name"] = m_name;         // у аккаунта = имя из профиля (мы его и передали)
    me["mic"] = m_mic;
    me["cam"] = m_cam;
    me["isSelf"] = true;
    me["speaking"] = false;
    m_participants.append(me);
    // 1) остальные — из join_ok.
    for (const QJsonValue& v : serverList) {
        const QJsonObject o = v.toObject();
        QVariantMap p;
        p["id"] = static_cast<qint64>(o.value("id").toDouble());
        p["name"] = o.value("name").toString();
        p["mic"] = o.value("mic").toBool(true);
        p["cam"] = o.value("cam").toBool(true);
        p["isSelf"] = false;
        p["speaking"] = false;
        m_participants.append(p);
    }
    emit participantsChanged();
}
// TODO: speaking обрабатывать на стороне клиента и менять тут

// Разослать участникам, что у нас с микрофоном/камерой (веб шлёт state).
void SignalingClient::setLocalState(bool mic, bool cam) {
    m_mic = mic; m_cam = cam;
    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState) {
        const QJsonObject st{ {"type","state"}, {"mic",mic}, {"cam",cam} };
        m_ws->sendTextMessage(QString::fromUtf8(
            QJsonDocument(st).toJson(QJsonDocument::Compact)));
    }
}

void SignalingClient::submitPassword(const QString& password) {
    m_roomPass = password;
    setError("");
    setPhase("connecting");
    sendJoin();                 // по тому же живому сокету, повторный join
}

void SignalingClient::leave() {
    m_manualClose = true;       // подавляем реконнект
    m_reconnectTimer->stop();
    m_waitTimer->stop();
    if (m_ws) { m_ws->close(); m_ws->deleteLater(); m_ws = nullptr; }
}

void SignalingClient::setPhase(const QString& p) {
    if (m_phase == p) return; m_phase = p; emit phaseChanged();
}
void SignalingClient::setError(const QString& t) {
    if (m_errorText == t) return; m_errorText = t; emit errorTextChanged();
}
void SignalingClient::fatal(const QString& t) {
    m_fatal = true;
    m_reconnectTimer->stop(); m_waitTimer->stop();
    if (m_ws) { m_ws->close(); }
    setError(t);
    setPhase("error");
}

void SignalingClient::onDisconnected() {
    if (m_manualClose || m_fatal) return;   // мы сами закрыли / фатальная ошибка
    m_attempts += 1;
    if (m_attempts > 8) {
        fatal("Соединение с сервером потеряно. Проверьте сеть и попробуйте ещё раз.");
        return;
    }
    m_reconnecting = true;
    emit reconnectingChanged();
    // пауза min(попытка,4) * 1.5 c
    const int delayMs = qMin(m_attempts, 4) * 1500;
    m_reconnectTimer->start(delayMs);
}

void SignalingClient::handleError(const QString& reason) {
    if (reason == "room_not_found") {
        m_manualClose = true;
        fatal("Комната не найдена или уже закрыта. Проверьте ссылку или создайте новую.");
    }
    else if (reason == "session_replaced") {
        // Тот же аккаунт вошёл в эту комнату в другом окне — здесь останавливаемся.
        // НЕ реконнектимся, иначе два окна выбивали бы друг друга по кругу.
        m_manualClose = true;
        fatal("Вы подключились к этой комнате в другом окне или на другом устройстве.");
    }
    else if (reason == "room_closed") {
        m_manualClose = true;
        fatal("Владелец завершил конференцию.");
    }
    else if (reason == "alias_forbidden") {
        m_manualClose = true;
        fatal("Эта ссылка-приглашение доступна только определённым участникам.");
    }
    else if (reason == "wrong_password") {
        // Личная комната с паролем: (пере)спрашиваем на гейте. Сокет НЕ закрыт.
        setError(m_roomPass.isEmpty() ? "" : "Неверный пароль. Попробуйте ещё раз.");
        m_roomPass.clear();
        setPhase("gate");
    }
    else if (reason == "room_offline") {
        // Личная комната без владельца ещё не существует: ждём и повторяем join.
        setPhase("waiting");
        if (!m_waitTimer->isActive()) m_waitTimer->start(5000);   // повтор через 5 с
    }
    else {
        // Неизвестная причина (список расширяемый) — логируем и живём дальше.
        qWarning() << "SignalingClient: неизвестная ошибка сервера:" << reason;
    }
}

void SignalingClient::sendChat(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return;                       // пустое/пробелы не шлём
    if (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState) return;

    const QJsonObject chat{ {"type", "chat"}, {"text", t} };
    m_ws->sendTextMessage(QString::fromUtf8(
        QJsonDocument(chat).toJson(QJsonDocument::Compact)));
    // В ленту НЕ добавляем — сервер вернёт это же сообщение как "chat".
}

QVariantMap SignalingClient::makeMessage(qint64 senderId, const QString& senderName,
    const QString& text, qint64 tsMs) {
    QVariantMap m;
    m["author"] = senderName.isEmpty() ? ("Участник " + QString::number(senderId))
        : senderName;
    m["text"] = text;
    m["time"] = QDateTime::fromMSecsSinceEpoch(tsMs).toString("HH:mm");
    m["self"] = (senderId == m_myId);
    return m;
}