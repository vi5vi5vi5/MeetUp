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

// Потолки истории чата. Это не удобство, а память: замер показал около 20 МБ
// на одну комнату, набитую картинками до предела, и держатся они ещё десять
// минут после ухода последнего участника. Значения задаёт владелец сервера.
struct ChatLimits
{
    int historySize = 500;     // сообщений в истории
    int historyImages = 24;    // из них с картинками
};

// Одна конференция (комната). Держит список участников и историю чата, умеет
// рассылать им сообщения. Медиа не декодирует — только пересылает байты как есть.
class ConferenceRoom
{
public:
    explicit ConferenceRoom(const QString &code, int ownerId = -1, ChatLimits chat = {});

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

    // Демонстрации экрана. Сколько их может идти одновременно, решает владелец
    // сервера (media.max_screen_shares); сверх лимита сервер отвечает
    // screen_busy. Кадры экрана от участника, за которым слот не закреплён, не
    // ретранслируются — иначе любой мог бы подмешать свою картинку в чужую
    // полосу. Выход ведущего освобождает слот (removeParticipant).
    const QList<ClientSession *> &screenSharers() const { return m_screenSharers; }
    bool isScreenSharer(ClientSession *session) const
    {
        return m_screenSharers.contains(session);
    }
    // true — слот есть (или уже был за этим участником).
    bool addScreenSharer(ClientSession *session, int limit);
    void removeScreenSharer(ClientSession *session) { m_screenSharers.removeAll(session); }

    // Рассылка всем участникам; except (если задан) пропускается.
    void broadcastJson(const QJsonObject &obj, ClientSession *except = nullptr) const;
    // dropIfBusy — можно ли пропустить это сообщение у получателя, который не
    // успевает его забирать. Для видеокадров можно и нужно (см. .cpp), для
    // звука и служебного — нельзя.
    void broadcastBinary(const QByteArray &data, ClientSession *except = nullptr,
                         bool dropIfBusy = false) const;

    // [{ "id": ..., "name": ..., "mic": ..., "cam": ... }, ...] по участникам.
    QJsonArray participantsJson() const;

    // Кто сейчас показывает экран — для join_ok.
    QJsonArray screenIdsJson() const;

    // История чата: хранится последние kMaxHistory сообщений, отдаётся
    // новому участнику в join_ok, чтобы он видел разговор до своего входа.
    void appendChat(const ChatEntry &entry);
    QJsonArray historyJson() const;

    // Порог backpressure для этой комнаты: растёт вместе с числом полос.
    qint64 slowClientBytes() const;

private:
    QString m_code;
    int m_ownerId = -1;
    QList<ClientSession *> m_sessions;        // не владеет сессиями
    QList<ClientSession *> m_screenSharers;   // кто ведёт демонстрации; не владеет
    QList<ChatEntry> m_history;
    qint64 m_emptySinceMs = 0;
    qint64 m_liveSinceMs = 0;

    // Картинки тяжелее текста на порядки: без потолка история могла бы
    // держать сотни мегабайт на комнату. Старые картинки вытесняются
    // (imageDropped), текст сообщений остаётся.
    ChatLimits m_chat;

    // Сколько неотправленного у получателя означает «он не поспевает». Порог
    // не берётся из воздуха: полтора мегабайта — это около полутора секунд
    // демонстрации на 8 Мбит/с, то есть заведомо больше любого разумного
    // всплеска (даже опорный кадр 4K вчетверо меньше) и заведомо меньше того,
    // что имеет смысл досылать: видео полуторасекундной давности не нужно
    // никому. См. broadcastBinary.
    //
    // Считается НА ПОЛОСУ, а не на сокет: очередь у получателя общая, и когда
    // в комнате идут три демонстрации, она наполняется втрое быстрее. С
    // неизменным порогом «полторы секунды» превратились бы в полсекунды, и
    // сервер начал бы резать видео там, где канал в порядке. Поэтому порог
    // умножается на число живых полос видео — см. slowClientBytes().
    static constexpr qint64 kSlowClientBytesPerLane = 1500000;
};
