#include "SignalingClient.h"
#include "SignalingLink.h"
#include "ApiClient.h"
#include "ChatImages.h"
#include "../crypto/E2eCipher.h"
#include <QThread>
#include <QTimer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>
#include <QDateTime>

SignalingClient::SignalingClient(ApiClient* api, E2eCipher* cipher, ChatImages* images,
    QObject* parent)
    : QObject(parent), m_api(api), m_cipher(cipher), m_images(images) {
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
    m_chat.reset();
    m_images->clear();      // и её картинки: держать их дальше незачем
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
        m_images->clear();     // ленту собираем заново — старые id больше не нужны
        {
            // Сервер отдаёт историю от старых к новым, лента хранит наоборот
            // (новейшее в индексе 0, см. ChatModel) — поэтому разворачиваем.
            const QJsonArray history = msg.value("history").toArray();
            QList<ChatModel::Row> rows;
            rows.reserve(history.size());
            for (int i = history.size() - 1; i >= 0; --i) {
                const QJsonObject h = history.at(i).toObject();
                rows.append(makeMessage(
                    static_cast<qint64>(h.value("sender_id").toDouble()),
                    h.value("sender_name").toString(),
                    h.value("text").toString(),
                    h.value("image").toString(),
                    h.value("image_dropped").toBool(),
                    static_cast<qint64>(h.value("timestamp_ms").toDouble())));
            }
            m_chat.setAll(std::move(rows));
        }
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
        const qint64 from = static_cast<qint64>(msg.value("sender_id").toDouble());
        m_chat.prepend(makeMessage(
            from,
            msg.value("sender_name").toString(),
            msg.value("text").toString(),
            msg.value("image").toString(),
            false,                        // живое сообщение вытеснить ещё не успели
            static_cast<qint64>(msg.value("timestamp_ms").toDouble())));
        emit chatArrived(from == m_myId);   // сервер возвращает и наши сообщения
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
    else if (reason == "image_too_large") {
        // Сервер отверг картинку. Своя лестница ужимания целится с запасом, так
        // что сюда попадают только шифрованные на самой границе — но молчать
        // нельзя: человек уверен, что отправил.
        emit chatImageFailed("Сервер не принял изображение: слишком большое.");
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
    QString t = text.trimmed();
    if (t.isEmpty()) return;                       // пустое/пробелы не шлём
    // С включённым ключом сервер получает и хранит только шифротекст — для
    // него это обычная строка сообщения.
    if (m_cipher->active()) {
        const QString sealed = m_cipher->sealText(t);
        if (sealed.isEmpty()) return;              // не смогли — молчим, а не шлём открытым
        t = sealed;
    }
    sendJson({ {"type", "chat"}, {"text", t} });
    // В ленту НЕ добавляем — сервер вернёт это же сообщение как "chat".
}

// ---------- картинки в чате ----------

void SignalingClient::sendImageFile(const QUrl& url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) return;

    QPointer<SignalingClient> self(this);
    QThreadPool::globalInstance()->start([self, path]() {
        // Читаем и ужимаем в стороне: снимок с телефона — это декод на
        // несколько мегапикселей плюс масштабирование, сотни миллисекунд.
        // На GUI-потоке это была бы заметная заминка окна.
        QImageReader r(path);
        r.setAutoTransform(true);          // учесть поворот из EXIF
        const QImage img = r.read();
        const QByteArray b64 = img.isNull() ? QByteArray() : packChatImage(img);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, b64, null = img.isNull()]() {
            if (!self) return;
            if (null)          emit self->chatImageFailed("Не удалось открыть изображение.");
            else if (b64.isEmpty()) emit self->chatImageFailed(
                "Изображение слишком большое — не удалось ужать его до предела сервера.");
            else self->sendImageB64(b64);
            }, Qt::QueuedConnection);
        });
}

bool SignalingClient::sendClipboardImage() {
    const QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) return false;       // в буфере не картинка — не наш случай

    QPointer<SignalingClient> self(this);
    QThreadPool::globalInstance()->start([self, img]() {
        const QByteArray b64 = packChatImage(img);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, b64]() {
            if (!self) return;
            if (b64.isEmpty()) emit self->chatImageFailed(
                "Изображение слишком большое — не удалось ужать его до предела сервера.");
            else self->sendImageB64(b64);
            }, Qt::QueuedConnection);
        });
    return true;
}

void SignalingClient::sendImageB64(const QByteArray& b64) {
    QString payload = QString::fromLatin1(b64);
    if (m_cipher->active()) {
        // Шифруем БАЙТЫ jpeg, а не их base64-запись — так делает веб, и это
        // единственный вариант, при котором картинка откроется в браузере.
        const QString sealed = m_cipher->sealImage(b64.isEmpty()
            ? QByteArray() : QByteArray::fromBase64(b64));
        if (sealed.isEmpty()) {           // не смогли — молчим, а не шлём открытым
            emit chatImageFailed("Не удалось зашифровать изображение.");
            return;
        }
        payload = sealed;
    }
    sendJson({ {"type", "chat"}, {"image", payload} });
}

QAbstractListModel* SignalingClient::messages() { return &m_chat; }

ChatModel::Row SignalingClient::makeMessage(qint64 senderId, const QString& senderName,
    const QString& text, const QString& image, bool imageDropped, qint64 tsMs) {
    ChatModel::Row r;
    r.author = senderName.isEmpty() ? ("Участник " + QString::number(senderId))
        : senderName;
    // Храним строки как пришли: ключ могут ввести уже после этого сообщения,
    // и тогда ленту перечитают (см. reReadMessages).
    r.raw = text;
    r.rawImage = image;
    // Картинка была, но сервер вытеснил её из истории (держит 24 свежие).
    // Отличать от «картинки не было» обязательно: иначе сообщение выглядит
    // пустым и человек решит, что потерялось всё.
    r.imageDropped = imageDropped;
    r.time = QDateTime::fromMSecsSinceEpoch(tsMs).toString("HH:mm");
    r.self = (senderId == m_myId);
    renderBody(r);
    return r;
}

// Как показать пришедшее: обычный текст — как есть, "🔒e2e:…" —
// расшифрованный. Ключа нет или он чужой — честная заглушка вместо текста:
// показать шифротекст было бы враньём, а промолчать — потерей сообщения.
// С картинкой ровно та же развилка, только заглушку рисует QML.
void SignalingClient::renderBody(ChatModel::Row& r) const {
    QString plain;
    if (!E2eCipher::isSealedText(r.raw)) {
        r.text = r.raw;
        r.locked = false;
    } else if (m_cipher->openText(r.raw, plain)) {
        r.text = plain;
        r.locked = false;
    } else {
        r.text.clear();
        r.locked = true;
    }

    r.image.clear();
    r.imageLocked = false;
    if (r.rawImage.isEmpty()) return;

    QByteArray jpeg;
    if (!E2eCipher::isSealedText(r.rawImage)) {
        jpeg = QByteArray::fromBase64(r.rawImage.toLatin1());
    } else if (!m_cipher->openImage(r.rawImage, jpeg)) {
        r.imageLocked = true;
        return;
    }
    if (jpeg.isEmpty()) { r.imageLocked = true; return; }
    // put() каждый раз выдаёт новый id — это и нужно: при перечитывании ленты
    // новым ключом у картинки обязан смениться URL, иначе QML возьмёт из кеша
    // прежний результат (то есть пустоту, ведь до ключа её не было).
    QSize size;
    r.image = "image://chatimg/" + m_images->put(jpeg, &size);
    r.imageW = size.width();
    r.imageH = size.height();
}

void SignalingClient::reReadMessages() {
    if (m_chat.isEmpty()) return;
    // Перечитываем всё подряд, поэтому старые id картинок сразу выбрасываем:
    // renderBody выдаст каждой новый, а иначе они копились бы при каждой
    // смене ключа.
    m_images->clear();
    m_chat.reRead([this](ChatModel::Row& r) { renderBody(r); });
}

// Кадры от видеодвижка (он живёт на GUI-потоке). Звук сюда не заходит: у
// аудиопотока прямая дорога в транспорт, см. mediaLink().
void SignalingClient::sendBinary(const QByteArray& frame) {
    if (!m_link->isConnected()) return;
    QMetaObject::invokeMethod(m_link, [this, frame] { m_link->sendBinary(frame); },
        Qt::QueuedConnection);
}