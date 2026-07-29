#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QHash>
#include "ChatModel.h"

class ApiClient;
class ChatImages;
class SignalingLink;
class E2eCipher;
class QThread;
class QTimer;
class QUrl;

// Одно WebSocket-соединение с комнатой: join, участники, чат.
// Виден из QML как Conf.
//
// Сам сокет живёт не здесь, а на отдельном потоке (SignalingLink): звук не
// должен зависеть от того, занят ли GUI-поток отрисовкой. Здесь остались
// состояние комнаты и разбор JSON — всё, что нужно интерфейсу.

class SignalingClient : public QObject {
    Q_OBJECT
        Q_PROPERTY(QString      phase        READ phase        NOTIFY phaseChanged)
        Q_PROPERTY(QString      errorText    READ errorText    NOTIFY errorTextChanged)
        Q_PROPERTY(QString      roomTitle    READ roomTitle    NOTIFY roomTitleChanged)
        Q_PROPERTY(bool         reconnecting READ reconnecting NOTIFY reconnectingChanged)
        Q_PROPERTY(QVariantList participants READ participants NOTIFY participantsChanged)
        // Модель, а не список: подмена списка целиком сбрасывала прокрутку
        // ленты в начало на каждом сообщении (см. ChatModel). Объект живёт
        // столько же, сколько мы, — отсюда CONSTANT.
        Q_PROPERTY(QAbstractListModel* messages READ messages CONSTANT)
        // Кто сейчас говорит. ОТДЕЛЬНОЕ свойство, а не поле в participants:
        // подсветка меняется каждые пол-секунды, и трогать participants значило
        // бы пересоздавать плитки Repeater'ом — видео моргало бы.
        Q_PROPERTY(QVariantList speakingIds  READ speakingIds  NOTIFY speakingChanged)
        // Свой sender_id — QML сравнивает с screenId, чтобы понять, чья демонстрация.
        Q_PROPERTY(qint64       myId         READ myId         NOTIFY myIdChanged)
        // Кто ведёт демонстрацию экрана; 0 — никто. Слот в комнате один (§4.3).
        Q_PROPERTY(qint64       screenId     READ screenId     NOTIFY screenChanged)
        // Время до сервера и обратно, мс. -1 — ещё не мерили (или связи нет).
        Q_PROPERTY(int          ping         READ ping         NOTIFY pingChanged)
public:
    SignalingClient(ApiClient* api, E2eCipher* cipher, ChatImages* images,
        QObject* parent = nullptr);
    ~SignalingClient() override;

    QString phase() const { return m_phase; }
    // Код комнаты — соль ключа E2E (см. E2eCipher::deriveKey).
    QString roomCode() const { return m_roomCode; }
    // Перечитать ленту чата новым ключом: сообщения хранятся как пришли, и
    // введённая посреди разговора фраза обязана расшифровать уже показанное —
    // иначе половина переписки навсегда останется замочками.
    void reReadMessages();
    QString errorText() const { return m_errorText; }
    QString roomTitle() const { return m_roomTitle; }
    bool reconnecting() const { return m_reconnecting; }
    QVariantList participants() const { return m_participants; }
    QAbstractListModel* messages();
    QVariantList speakingIds() const { return m_speakingIds; }
    qint64 myId() const { return m_myId; }
    qint64 screenId() const { return m_screenId; }
    int ping() const { return m_ping; }

    // Подсветить говорящего на ~450 мс (как веб). Зовёт AudioEngine по RMS,
    // не QML — поэтому не Q_INVOKABLE.
    void markSpeaking(qint64 id);
    void markSelfSpeaking();

    // Войти в комнату. Вызывается из ConferenceScreen.Component.onCompleted.
    Q_INVOKABLE void open(const QString& roomCode, const QString& name);
    // Отправить пароль после гейта и повторить join (личная комната).
    Q_INVOKABLE void submitPassword(const QString& password);
    // Локальные микрофон/камера -> разослать участникам (state). Необязательно.
    Q_INVOKABLE void setLocalState(bool mic, bool cam);
    // Выйти из комнаты (закрыть сокет без реконнекта).
    // Запросить/отпустить слот демонстрации экрана (§4.3). Кадры пойдут только
    // после подтверждения сервера — VideoEngine слушает screenChanged.
    Q_INVOKABLE void setScreenShare(bool on);
    Q_INVOKABLE void leave();
    Q_INVOKABLE void sendChat(const QString& text);
    // Отправить картинку. url — то, что дал FileDialog (file:///…).
    // Ужимание и шифрование считаются в стороне от GUI-потока: декод и
    // масштаб снимка с телефона — это сотни миллисекунд, а окно в это время
    // рисует чужое видео.
    Q_INVOKABLE void sendImageFile(const QUrl& url);
    // Скриншот из буфера обмена (Ctrl+V в поле сообщения), как у веба.
    // Возвращает false, если в буфере картинки нет, — тогда QML не мешает
    // обычной вставке текста.
    Q_INVOKABLE bool sendClipboardImage();
    void sendBinary(const QByteArray& frame); // не Q_INVOKABLE - зовёт C++ медиадвижок, не QML

    // Неотправленный остаток в сокете (байты). Backpressure §5.5: выше 1.5 МБ
    // видеокадры пропускаются (аудио продолжает идти).
    qint64 bufferedBytes() const;

    // Транспорт — для аудиодвижка. Он подписывается на полосу звука напрямую,
    // в обход GUI-потока, и туда же отдаёт свои пакеты: пройти через нас значило
    // бы вернуть звук в очередь, которая на ресайзе окна и встаёт.
    SignalingLink* mediaLink() const { return m_link; }
    // Входящие байты за период — для MediaStats (считает раз в секунду).
    qint64 takeRxBytes();

signals:
    void phaseChanged();
    void errorTextChanged();
    void roomTitleChanged();
    void reconnectingChanged();
    void participantsChanged();
    void speakingChanged();
    void myIdChanged();
    void screenChanged();
    void pingChanged();
    void screenBusy();       // слот занят другим — показать уведомление
    // Первый успешный вход в комнату (не реконнект) — для локальной истории.
    void joinedRoom(const QString& code, const QString& title);

    // Сырой кадр v2 от сервера — всё, кроме звука: полосу звука транспорт
    // уводит прямо на аудиопоток, сюда она не заходит (см. mediaLink()).
    void binaryFrame(const QByteArray& frame);
    void joinOk();                              // каждый join_ok — И РЕКОННЕКТ тоже
    void participantJoined(qint64 id);          // новичок: вещатели шлют keyframe
    void participantLeft(qint64 id);            // участник ушёл (снести его декодер)
    void localStateChanged(bool mic, bool cam); // локальные микрофон/камера
    void left();    // вышли из комнаты (leave) — медиа немедленно глушится
    // Живое входящее сообщение (self — своё ли оно). Отдельно от
    // messagesChanged: тот летит ещё от истории в join_ok и от reReadMessages()
    // при смене ключа, а звук уведомления такие поводы устраивать не должны.
    void chatArrived(bool self);
    // Картинку не приняли (не открылась, не влезла, сервер отказал). Текст
    // готов к показу — QML только выводит его пилюлей.
    void chatImageFailed(const QString& text);

private:
    void openSocket();
    void sendJoin();
    void onConnected();
    void onTextMessage(const QString& text);
    void onDisconnected();
    void onJson(const QJsonObject& msg);
    void handleError(const QString& reason);

    void rebuildParticipants(const QJsonArray& serverList);
    void setScreenId(qint64 id);
    void setPing(int ms);
    void sendPing();                    // {"type":"ping"} — сервер вернёт t как есть
    void sendJson(const QJsonObject& msg);
    void rebuildSpeaking();             // m_speakingUntil -> m_speakingIds
    void sweepSpeaking();               // снять подсветку с отговоривших
    ChatModel::Row makeMessage(qint64 senderId, const QString& senderName,
        const QString& text, const QString& image, bool imageDropped, qint64 tsMs);
    // raw/rawImage -> text/locked/image текущим ключом.
    void renderBody(ChatModel::Row& r) const;
    void sendImageB64(const QByteArray& b64);   // общий хвост обоих источников
    void setPhase(const QString& p);
    void setError(const QString& t);
    void fatal(const QString& t);


    ApiClient* m_api;
    E2eCipher* m_cipher;                // не владеем: общий на всё приложение
    ChatImages* m_images;               // не владеем: принадлежит движку QML
    QThread* m_netThread = nullptr;     // поток транспорта
    SignalingLink* m_link = nullptr;    // живёт на m_netThread
    QTimer* m_reconnectTimer = nullptr;
    QTimer* m_waitTimer = nullptr;      // room_offline: повтор join
    QTimer* m_pingTimer = nullptr;      // замер RTT раз в 4 с, как у веба

    QString m_roomCode, m_name, m_roomPass;
    qint64  m_myId = 0;                      // sender_id (у аккаунта > 2^31 -> qint64!)
    bool    m_mic = false, m_cam = false;    // локальное состояние (по умолчанию выкл.)
    int     m_attempts = 0;
    bool    m_manualClose = false, m_fatal = false;
    bool    m_joinedOnce = false;            // защита от записи истории на каждом реконнекте
    int     m_ping = -1;                     // RTT, мс; -1 — не измерен
    qint64  m_screenId = 0;                  // ведущий демонстрации (0 — никто)
    bool    m_wantScreen = false;            // мы хотим слот: повторяем заявку после реконнекта

    QString m_phase = "connecting";
    QString m_errorText, m_roomTitle;
    bool    m_reconnecting = false;
    QVariantList m_participants;             // список {id,name,mic,cam,isSelf,speaking,avatarUrl}

    ChatModel m_chat;          // лента: строки вставляются, а не подменяются

    QHash<qint64, qint64> m_speakingUntil;   // id -> до какого мс подсвечен
    QVariantList m_speakingIds;              // те же id для QML
    QTimer* m_speakTimer = nullptr;          // гасит подсветку по истечении
};