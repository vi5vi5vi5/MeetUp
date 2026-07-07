#pragma once

#include <QString>
#include <QList>
#include <QByteArray>
#include <QJsonArray>

class ClientSession;
class QJsonObject;

// Одна конференция (комната). Держит список участников и умеет рассылать
// им сообщения. Медиа не декодирует — только пересылает байты как есть.
class ConferenceRoom
{
public:
    explicit ConferenceRoom(const QString &code);

    QString code() const { return m_code; }
    bool isEmpty() const { return m_sessions.isEmpty(); }
    const QList<ClientSession *> &sessions() const { return m_sessions; }

    void addParticipant(ClientSession *session);
    void removeParticipant(ClientSession *session);

    // Рассылка всем участникам; except (если задан) пропускается.
    void broadcastJson(const QJsonObject &obj, ClientSession *except = nullptr) const;
    void broadcastBinary(const QByteArray &data, ClientSession *except = nullptr) const;

    // [{ "id": ..., "name": ... }, ...] по текущим участникам.
    QJsonArray participantsJson() const;

private:
    QString m_code;
    QList<ClientSession *> m_sessions;   // не владеет сессиями
};
