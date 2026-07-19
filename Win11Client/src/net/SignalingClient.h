#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QHash>

class ApiClient;
class QWebSocket;
class QTimer;

// Одно WebSocket-соединение с комнатой: join, участники, чат.
// Виден из QML как Conf.

class SignalingClient : public QObject {
    Q_OBJECT
        Q_PROPERTY(QString      phase        READ phase        NOTIFY phaseChanged)
        Q_PROPERTY(QString      errorText    READ errorText    NOTIFY errorTextChanged)
        Q_PROPERTY(QString      roomTitle    READ roomTitle    NOTIFY roomTitleChanged)
        Q_PROPERTY(bool         reconnecting READ reconnecting NOTIFY reconnectingChanged)
        Q_PROPERTY(QVariantList participants READ participants NOTIFY participantsChanged)
        Q_PROPERTY(QVariantList messages     READ messages     NOTIFY messagesChanged)
        // Кто сейчас говорит. ОТДЕЛЬНОЕ свойство, а не поле в participants:
        // подсветка меняется каждые пол-секунды, и трогать participants значило
        // бы пересоздавать плитки Repeater'ом — видео моргало бы.
        Q_PROPERTY(QVariantList speakingIds  READ speakingIds  NOTIFY speakingChanged)
public:
    explicit SignalingClient(ApiClient* api, QObject* parent = nullptr);
    ~SignalingClient() override;

    QString phase() const { return m_phase; }
    QString errorText() const { return m_errorText; }
    QString roomTitle() const { return m_roomTitle; }
    bool reconnecting() const { return m_reconnecting; }
    QVariantList participants() const { return m_participants; }
    QVariantList messages() const { return m_messages; }
    QVariantList speakingIds() const { return m_speakingIds; }

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
    Q_INVOKABLE void leave();
    Q_INVOKABLE void sendChat(const QString& text);
    void sendBinary(const QByteArray& frame); // не Q_INVOKABLE - зовёт C++ медиадвижок, не QML

    // Неотправленный остаток в сокете (байты). Backpressure §5.5: выше 1.5 МБ
    // видеокадры пропускаются (аудио продолжает идти).
    qint64 bufferedBytes() const { return m_bufferedBytes; }

signals:
    void phaseChanged();
    void errorTextChanged();
    void roomTitleChanged();
    void reconnectingChanged();
    void participantsChanged();
    void speakingChanged();
    // Первый успешный вход в комнату (не реконнект) — для локальной истории.
    void joinedRoom(const QString& code, const QString& title);
    void messagesChanged();

    void binaryFrame(const QByteArray& frame); // сырой кадр v2 от сервера
    void joinOk();                              // каждый join_ok — И РЕКОННЕКТ тоже
    void participantJoined(qint64 id);          // новичок: вещатели шлют keyframe
    void participantLeft(qint64 id);            // участник ушёл (снести его декодер)
    void localStateChanged(bool mic, bool cam); // локальные микрофон/камера
    void left();    // вышли из комнаты (leave) — медиа немедленно глушится

private:
    void openSocket();
    void sendJoin();
    void onConnected();
    void onTextMessage(const QString& text);
    void onDisconnected();
    void onJson(const QJsonObject& msg);
    void handleError(const QString& reason);

    void rebuildParticipants(const QJsonArray& serverList);
    void rebuildSpeaking();             // m_speakingUntil -> m_speakingIds
    void sweepSpeaking();               // снять подсветку с отговоривших
    QVariantMap makeMessage(qint64 senderId, const QString& senderName,
        const QString& text, qint64 tsMs);
    void setPhase(const QString& p);
    void setError(const QString& t);
    void fatal(const QString& t);


    ApiClient* m_api;
    QWebSocket* m_ws = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    QTimer* m_waitTimer = nullptr;      // room_offline: повтор join

    QString m_roomCode, m_name, m_roomPass;
    qint64  m_myId = 0;                      // sender_id (у аккаунта > 2^31 -> qint64!)
    bool    m_mic = true, m_cam = true;      // локальное состояние для self-плитки
    int     m_attempts = 0;
    bool    m_manualClose = false, m_fatal = false;
    bool    m_joinedOnce = false;            // защита от записи истории на каждом реконнекте

    qint64  m_bufferedBytes = 0;             // учёт неотправленного (backpressure)

    QString m_phase = "connecting";
    QString m_errorText, m_roomTitle;
    bool    m_reconnecting = false;
    QVariantList m_participants;             // список {id,name,mic,cam,isSelf,speaking,avatarUrl}

    QVariantList m_messages;   // список {author, text, time, self}

    QHash<qint64, qint64> m_speakingUntil;   // id -> до какого мс подсвечен
    QVariantList m_speakingIds;              // те же id для QML
    QTimer* m_speakTimer = nullptr;          // гасит подсветку по истечении
};