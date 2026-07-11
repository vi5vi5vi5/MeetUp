#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>

class QWebSocket;
class QJsonObject;
class ConferenceRoom;

// Одна сессия = одно WebSocket-соединение с клиентом.
// Оборачивает QWebSocket, хранит присвоенный сервером id, имя и текущую комнату.
// Сигналы сессии транслируют события сокета серверу, добавляя указатель на себя,
// чтобы сервер знал, от кого пришло сообщение.
class ClientSession : public QObject
{
    Q_OBJECT
public:
    explicit ClientSession(QWebSocket *socket, QObject *parent = nullptr);
    ~ClientSession() override;

    quint32 id() const { return m_id; }
    void setId(quint32 id) { m_id = id; }

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    // Аккаунт, привязанный к соединению по куке сессии из WS-хендшейка.
    // -1 — аноним. Авторизованный получает постоянный id и имя из профиля.
    int userId() const { return m_userId; }
    QString accountName() const { return m_accountName; }
    int avatarVer() const { return m_avatarVer; }
    void setAccount(int userId, const QString &displayName, int avatarVer)
    {
        m_userId = userId;
        m_accountName = displayName;
        m_avatarVer = avatarVer;
    }

    ConferenceRoom *room() const { return m_room; }
    void setRoom(ConferenceRoom *room) { m_room = room; }

    // Состояние медиа участника (сообщение "state" от клиента); раздаётся
    // остальным, чтобы вместо пустой плитки показывать заглушку и mic-off.
    bool micOn() const { return m_micOn; }
    void setMicOn(bool on) { m_micOn = on; }
    bool camOn() const { return m_camOn; }
    void setCamOn(bool on) { m_camOn = on; }

    // Отправка сообщений этому клиенту.
    void sendJson(const QJsonObject &obj);
    void sendBinary(const QByteArray &data);

    // Вежливое закрытие сокета; disconnected придёт обычным путём.
    void close();

signals:
    void textReceived(ClientSession *self, const QString &text);
    void binaryReceived(ClientSession *self, const QByteArray &data);
    void disconnected(ClientSession *self);

private slots:
    void onTextMessageReceived(const QString &text);
    void onBinaryMessageReceived(const QByteArray &data);
    void onSocketDisconnected();

private:
    QWebSocket *m_socket;
    quint32 m_id = 0;
    QString m_name;
    int m_userId = -1;                  // id аккаунта; -1 — аноним
    QString m_accountName;              // отображаемое имя из профиля
    int m_avatarVer = 0;                // версия аватарки на момент подключения
    ConferenceRoom *m_room = nullptr;   // nullptr до join
    bool m_micOn = true;
    bool m_camOn = true;
};
