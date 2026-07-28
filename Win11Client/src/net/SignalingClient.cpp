#include "SignalingClient.h"
#include "SignalingLink.h"
#include "ApiClient.h"
#include <QThread>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDateTime>

SignalingClient::SignalingClient(ApiClient* api, QObject* parent)
    : QObject(parent), m_api(api) {
    // Транспорт на своём потоке: кадры звука приходят каждые 20 мс и не должны
    // ждать, пока GUI-поток закончит ресайз окна. Сюда, на GUI, возвращаются
    // только текстовые сообщения и видео — им задержка не страшна.
    m_netThread = new QThread(this);
    m_netThread->setObjectName("signaling-net");
    m_link = new SignalingLink;              // без parent: переезжает на свой поток
    m_link->moveToThread(m_netThread);
    connect(m_netThread, &QThread::finished, m_link, &QObject::deleteLater);
    connect(m_link, &SignalingLink::opened, this, &SignalingClient::onConnected);
    connect(m_link, &SignalingLink::closed, this, &SignalingClient::onDisconnected);
    connect(m_link, &SignalingLink::textMessage, this, &SignalingClient::onTextMessage);
    // Видео и служебные кадры — как раньше, наружу как есть. Полоса звука
    // (audioFrame) сюда не заходит: на неё подписан AudioEngine напрямую.
    connect(m_link, &SignalingLink::binaryFrame, this, &SignalingClient::binaryFrame);
    m_netThread->start();

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_manualClose && !m_fatal) openSocket();
        });
    m_waitTimer = new QTimer(this);
    m_waitTimer->setSingleShot(true);
    connect(m_waitTimer, &QTimer::timeout, this, [this]() { sendJoin(); });
    // Гаснет подсветка «говорит»: тикает только пока кто-то говорит.
    m_speakTimer = new QTimer(this);
    m_speakTimer->setInterval(120);
    connect(m_speakTimer, &QTimer::timeout, this, &SignalingClient::sweepSpeaking);
    // Замер задержки до сервера: метка времени туда — эхо обратно (§4.1).
    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(4000);              // как у веба
    connect(m_pingTimer, &QTimer::timeout, this, &SignalingClient::sendPing);
}

// ---------- индикатор «говорит» ----------

void SignalingClient::markSpeaking(qint64 id) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool wasOn = m_speakingUntil.value(id, 0) > now;
    m_speakingUntil[id] = now + 450;        // окно подсветки — как у веба
    if (!wasOn) rebuildSpeaking();
    if (!m_speakTimer->isActive()) m_speakTimer->start();
}

void SignalingClient::markSelfSpeaking() {
    if (m_myId) markSpeaking(m_myId);
}

void SignalingClient::rebuildSpeaking() {
    m_speakingIds.clear();
    for (auto it = m_speakingUntil.constBegin(); it != m_speakingUntil.constEnd(); ++it)
        m_speakingIds.append(it.key());
    emit speakingChanged();
}

void SignalingClient::sweepSpeaking() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (auto it = m_speakingUntil.begin(); it != m_speakingUntil.end(); ) {
        if (it.value() <= now) { it = m_speakingUntil.erase(it); changed = true; }
        else ++it;
    }
    if (changed) rebuildSpeaking();
    if (m_speakingUntil.isEmpty()) m_speakTimer->stop();
}

SignalingClient::~SignalingClient() {
    // Сокет закрывается на своём потоке, иначе QWebSocket разрушался бы из
    // чужого. Блокирующий вызов безопасен: поток крутит свой цикл событий и
    // нас не ждёт.
    QMetaObject::invokeMethod(m_link, &SignalingLink::shutdown, Qt::BlockingQueuedConnection);
    m_netThread->quit();
    m_netThread->wait();        // finished -> deleteLater транспорта
}

qint64 SignalingClient::bufferedBytes() const { return m_link->bufferedBytes(); }
qint64 SignalingClient::takeRxBytes() { return m_link->takeRxBytes(); }

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
    // Лента прошлой комнаты: join_ok пересоберёт её заново, но до его прихода
    // в новой комнате висела бы чужая переписка.
    if (!m_messages.isEmpty()) { m_messages.clear(); emit messagesChanged(); }
    // Остатки прошлой конференции: заголовок личной комнаты и флаг реконнекта.
    if (!m_roomTitle.isEmpty()) { m_roomTitle.clear(); emit roomTitleChanged(); }
    if (m_reconnecting) { m_reconnecting = false; emit reconnectingChanged(); }
    setError("");
    setPhase("connecting");
    openSocket();
}

void SignalingClient::openSocket() {
    // Адрес и куку читаем здесь: ApiClient живёт на GUI-потоке, и транспорту
    // они уезжают значениями.
    const QUrl url(m_api->wsUrl());
    const QString token = m_api->sessionToken();
    QMetaObject::invokeMethod(m_link, [this, url, token] { m_link->open(url, token); },
        Qt::QueuedConnection);
}

void SignalingClient::onConnected() {
    m_reconnecting = false;
    emit reconnectingChanged();
    sendJoin();
}

// Первое сообщение в комнату — представляемся.
void SignalingClient::sendJoin() {
    if (!m_link->isConnected()) { openSocket(); return; }
    const QJsonObject join{
        {"type", "join"},
        {"room", m_roomCode},
        {"name", m_name},           // у аккаунта сервер проигнорирует, возьмёт из профиля
        {"password", m_roomPass},   // нужен только гостю личной комнаты с паролем
        // Своё состояние — сразу в join: иначе сервер заведёт нас со своим
        // умолчанием (mic/cam = on) и разошлёт остальным participant_joined с
        // включённой камерой, а наш state{off} догонит лишь через круг —
        // собеседники успевают увидеть «камера подключается».
        {"mic", m_mic},
        {"cam", m_cam},
    };
    sendJson(join);
}
void SignalingClient::onTextMessage(const QString& text) {
    const QJsonObject msg =
        QJsonDocument::fromJson(text.toUtf8()).object();
    onJson(msg);
    
}

// Аватарка участника: id и версия приходят в participant_joined/participants
// (только у авторизованных). Версия в URL — кэш-ключ, как в Auth::avatarUrl.
static QString participantAvatarUrl(ApiClient* api, const QJsonObject& o) {
    const qint64 uid = static_cast<qint64>(o.value("user_id").toDouble());
    const int ver = o.value("avatar").toInt();
    if (uid <= 0 || ver <= 0) return {};
    return api->baseUrl() + "/api/users/" + QString::number(uid)
        + "/avatar?v=" + QString::number(ver);
}

void SignalingClient::onJson(const QJsonObject& msg) {
    const QString type = msg.value("type").toString();

    if (type == "join_ok") {
        m_myId = static_cast<qint64>(msg.value("sender_id").toDouble()); // >2^31 у аккаунта
        emit myIdChanged();
        m_attempts = 0;
        if (m_waitTimer->isActive()) m_waitTimer->stop();
        const QString title = msg.value("room_title").toString();
        if (!title.isEmpty()) { m_roomTitle = title; emit roomTitleChanged(); }

        // Реконнект: у анонимов id новые — старая подсветка бессмысленна.
        m_speakingUntil.clear();
        rebuildSpeaking();
        rebuildParticipants(msg.value("participants").toArray());
        setError("");
        setPhase("live");
        // Разослать своё состояние микрофона/камеры (как делает веб после join_ok).
        setLocalState(m_mic, m_cam);
        if (!m_joinedOnce) {          // реконнект историю не задваивает (как веб)
            m_joinedOnce = true;
            emit joinedRoom(m_roomCode, title);   // title пуст у разовых комнат
        }
        // Демонстрация экрана (§4.3): сервер сообщает, идёт ли она и от кого.
        // Свой слот после обрыва не сохраняется — если мы вещали, просим заново.
        setScreenId(msg.contains("screen_id")
            ? static_cast<qint64>(msg.value("screen_id").toDouble()) : 0);
        emit joinOk();   // каждый вход, включая реконнект: медиа сбрасывает буферы
        if (m_wantScreen && m_screenId == 0)
            sendJson({ {"type","screen"}, {"on", true} });
        sendPing();                  // первый замер сразу, дальше по таймеру
        m_pingTimer->start();
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
        p["avatarUrl"] = participantAvatarUrl(m_api, msg);
        // не дублируем, если такой id уже есть
        for (const QVariant& v : m_participants)
            if (v.toMap().value("id").toLongLong() == p["id"].toLongLong()) return;
        m_participants.append(p);
        emit participantsChanged();
        // Вещатели обязаны прислать новичку опорный кадр (§4.2) — не ждать 3 с.
        emit participantJoined(p["id"].toLongLong());
        return;
    }

    if (type == "participant_left") {
        const qint64 id = static_cast<qint64>(msg.value("id").toDouble());
        for (int i = 0; i < m_participants.size(); ++i)
            if (m_participants[i].toMap().value("id").toLongLong() == id) {
                m_participants.removeAt(i);
                emit participantsChanged();
                emit participantLeft(id);
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

    if (type == "pong") {
        // Сервер вернул нашу же метку — разница и есть время туда-обратно.
        const qint64 sent = static_cast<qint64>(msg.value("t").toDouble());
        setPing(int(qMax(qint64(0), QDateTime::currentMSecsSinceEpoch() - sent)));
        return;
    }

    // Подтверждение слота демонстрации — всем, включая инициатора (§4.3).
    if (type == "screen") {
        const qint64 id = static_cast<qint64>(msg.value("id").toDouble());
        const bool on = msg.value("on").toBool();
        if (on) {
            if (id == m_myId) m_wantScreen = true;
            setScreenId(id);
        } else if (m_screenId == id) {
            if (id == m_myId) m_wantScreen = false;
            setScreenId(0);
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
    me["avatarUrl"] = "";        // своя аватарка берётся из Auth в QML
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
        p["avatarUrl"] = participantAvatarUrl(m_api, o);
        m_participants.append(p);
    }
    emit participantsChanged();
}
// TODO: speaking обрабатывать на стороне клиента и менять тут

// Разослать участникам, что у нас с микрофоном/камерой (веб шлёт state).
void SignalingClient::sendJson(const QJsonObject& msg) {
    if (!m_link->isConnected()) return;
    const QString text = QString::fromUtf8(
        QJsonDocument(msg).toJson(QJsonDocument::Compact));
    QMetaObject::invokeMethod(m_link, [this, text] { m_link->sendText(text); },
        Qt::QueuedConnection);
}

void SignalingClient::setLocalState(bool mic, bool cam) {
    m_mic = mic; m_cam = cam;
    emit localStateChanged(m_mic, m_cam);   // AudioEngine глушит/включает захват
    sendJson({ {"type","state"}, {"mic",mic}, {"cam",cam} });
}

void SignalingClient::setPing(int ms) {
    if (m_ping == ms) return;
    m_ping = ms;
    emit pingChanged();
}

void SignalingClient::sendPing() {
    sendJson({ {"type","ping"}, {"t", double(QDateTime::currentMSecsSinceEpoch())} });
}

void SignalingClient::setScreenId(qint64 id) {
    if (m_screenId == id) return;
    m_screenId = id;
    emit screenChanged();
}

void SignalingClient::setScreenShare(bool on) {
    m_wantScreen = on;
    // Свою сцену гасим сразу, не дожидаясь эха сервера (так же делает веб) —
    // иначе кнопка «залипала» бы на время круга до сервера и обратно.
    if (!on && m_screenId == m_myId) setScreenId(0);
    sendJson({ {"type","screen"}, {"on", on} });
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
    m_speakTimer->stop();
    m_pingTimer->stop();
    setPing(-1);
    m_speakingUntil.clear();
    rebuildSpeaking();
    m_wantScreen = false;
    setScreenId(0);
    // Выход = чистый следующий вход. Экран конференции в QML пересоздаётся с
    // микрофоном/камерой «выкл», а SignalingClient и движки живут всё
    // приложение — если не сбросить, включённая в прошлый раз камера осталась
    // бы в m_cam/m_camOn и на новом входе тихо включилась бы (sendJoin шлёт
    // m_cam, а phase→live поднимает захват), пока интерфейс показывает «выкл».
    if (m_mic || m_cam) {
        m_mic = false;
        m_cam = false;
        emit localStateChanged(false, false);   // движки гасят захват и свои тумблеры
    }
    // Закрытие с close-фреймом, чтобы сервер увидел «участник вышел», а не
    // «пропал» (подробности — в SignalingLink::closeGracefully).
    QMetaObject::invokeMethod(m_link, &SignalingLink::closeGracefully, Qt::QueuedConnection);
    emit left();
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
    QMetaObject::invokeMethod(m_link, &SignalingLink::close, Qt::QueuedConnection);
    setError(t);
    setPhase("error");
}

void SignalingClient::onDisconnected() {
    m_pingTimer->stop();          // мерить нечего, и старое число врало бы
    setPing(-1);
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
    else if (reason == "screen_busy") {
        // Слот демонстрации занят другим: захват мы ещё не начинали (ждём
        // подтверждения), поэтому достаточно снять заявку и сказать об этом.
        m_wantScreen = false;
        emit screenBusy();
    }
    else {
        // Неизвестная причина (список расширяемый) — логируем и живём дальше.
        qWarning() << "SignalingClient: неизвестная ошибка сервера:" << reason;
    }
}

void SignalingClient::sendChat(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return;                       // пустое/пробелы не шлём
    sendJson({ {"type", "chat"}, {"text", t} });
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

// Кадры от видеодвижка (он живёт на GUI-потоке). Звук сюда не заходит: у
// аудиопотока прямая дорога в транспорт, см. mediaLink().
void SignalingClient::sendBinary(const QByteArray& frame) {
    if (!m_link->isConnected()) return;
    QMetaObject::invokeMethod(m_link, [this, frame] { m_link->sendBinary(frame); },
        Qt::QueuedConnection);
}