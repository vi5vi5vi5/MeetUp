#pragma once

#include <QString>
#include <QList>
#include <QByteArray>
#include <QJsonArray>

class ClientSession;
class QJsonObject;

// Одно сообщение чата, сохранённое в истории комнаты. Имя фиксируется на
// момент отправки: участник может выйти, а история должна остаться читаемой.
struct ChatEntry
{
    quint32 senderId = 0;
    QString senderName;
    QString text;
    qint64  timestampMs = 0;
    QString image;               // JPEG в base64 (или шифротекст E2E); пусто — нет картинки
    bool    imageDropped = false; // картинка была, но вытеснена из истории (см. appendChat)
};

// Одна конференция (комната). Держит список участников и историю чата, умеет
// рассылать им сообщения. Медиа не декодирует — только пересылает байты как есть.
class ConferenceRoom
{
public:
    explicit ConferenceRoom(const QString &code, int ownerId = -1);

    QString code() const { return m_code; }

    // Личная комната открыта владельцем: join-правила (пароль, присутствие
    // владельца) сервер проверяет по свежим данным PersonalRoomService.
    // -1 — обычная разовая конференция.
    int ownerId() const { return m_ownerId; }

    bool isEmpty() const { return m_sessions.isEmpty(); }
    const QList<ClientSession *> &sessions() const { return m_sessions; }

    // С какого момента комната стоит пустой (мс эпохи); 0 — внутри есть люди.
    // Свежесозданная комната считается пустой с момента создания. По этой
    // метке RoomRegistry удаляет брошенные комнаты.
    qint64 emptySinceMs() const { return m_emptySinceMs; }

    // С какого момента комната непрерывно не пуста; 0 — сейчас пусто.
    // Главная показывает владельцу «в эфире · длительность».
    qint64 liveSinceMs() const { return m_liveSinceMs; }

    void addParticipant(ClientSession *session);
    void removeParticipant(ClientSession *session);

    // Демонстрация экрана: в комнате может идти только одна. Сервер закрепляет
    // её за участником и отклоняет остальных (error screen_busy); кадры
    // экрана от других участников не ретранслируются. Выход ведущего из
    // комнаты снимает закрепление (removeParticipant).
    ClientSession *screenSharer() const { return m_screenSharer; }
    void setScreenSharer(ClientSession *session) { m_screenSharer = session; }

    // Рассылка всем участникам; except (если задан) пропускается.
    void broadcastJson(const QJsonObject &obj, ClientSession *except = nullptr) const;
    void broadcastBinary(const QByteArray &data, ClientSession *except = nullptr) const;

    // [{ "id": ..., "name": ..., "mic": ..., "cam": ... }, ...] по участникам.
    QJsonArray participantsJson() const;

    // История чата: хранится последние kMaxHistory сообщений, отдаётся
    // новому участнику в join_ok, чтобы он видел разговор до своего входа.
    void appendChat(const ChatEntry &entry);
    QJsonArray historyJson() const;

private:
    QString m_code;
    int m_ownerId = -1;
    QList<ClientSession *> m_sessions;   // не владеет сессиями
    ClientSession *m_screenSharer = nullptr;   // кто ведёт демонстрацию; не владеет
    QList<ChatEntry> m_history;
    qint64 m_emptySinceMs = 0;
    qint64 m_liveSinceMs = 0;

    static constexpr int kMaxHistory = 500;

    // Картинки тяжелее текста на порядки: без потолка история из kMaxHistory
    // сообщений могла бы держать сотни мегабайт на комнату. Старые картинки
    // вытесняются (imageDropped), текст сообщений остаётся.
    static constexpr int kMaxHistoryImages = 24;
};
