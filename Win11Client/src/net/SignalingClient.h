#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>

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
public:
    explicit SignalingClient(ApiClient* api, QObject* parent = nullptr);
    ~SignalingClient() override;

    QString phase() const { return m_phase; }
    QString errorText() const { return m_errorText; }
    QString roomTitle() const { return m_roomTitle; }
    bool reconnecting() const { return m_reconnecting; }
    QVariantList participants() const { return m_participants; }
    QVariantList messages() const { return m_messages; }

    // Войти в комнату. Вызывается из ConferenceScreen.Component.onCompleted.
    Q_INVOKABLE void open(const QString& roomCode, const QString& name);
    // Отправить пароль после гейта и повторить join (личная комната).
    Q_INVOKABLE void submitPassword(const QString& password);
    // Локальные микрофон/камера -> разослать участникам (state). Необязательно.
    Q_INVOKABLE void setLocalState(bool mic, bool cam);
    // Выйти из комнаты (закрыть сокет без реконнекта).
    Q_INVOKABLE void leave();
    Q_INVOKABLE void sendChat(const QString& text);

signals:
    void phaseChanged();
    void errorTextChanged();
    void roomTitleChanged();
    void reconnectingChanged();
    void participantsChanged();
    void messagesChanged();

private:
    void openSocket();
    void sendJoin();
    void onConnected();
    void onTextMessage(const QString& text);
    void onDisconnected();
    void onJson(const QJsonObject& msg);
    void handleError(const QString& reason);

    void rebuildParticipants(const QJsonArray& serverList);
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

    QString m_phase = "connecting";
    QString m_errorText, m_roomTitle;
    bool    m_reconnecting = false;
    QVariantList m_participants;             // список {id,name,mic,cam,isSelf,speaking}

    QVariantList m_messages;   // список {author, text, time, self}
};