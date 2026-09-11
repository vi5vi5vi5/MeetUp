#pragma once
#include <QObject>
#include <QString>

class ApiClient;

// Портрет сервера (GET /api/config): как он называется, что на нём разрешено
// и из какого коммита собран. Виден из QML как Server.
//
// Зачем это клиенту. Сервер MeetUp поднимает кто угодно и настраивает под
// себя: у одного регистрация открыта, у другого закрыта, у третьего разовые
// комнаты заводят только из аккаунта. Кнопка, которая всегда отвечает
// отказом, хуже отсутствующей — поэтому экраны спрашивают здесь, что рисовать.
//
// Правило на неизвестное: отсутствующее поле — РАЗРЕШАЮЩЕЕ. Старый сервер про
// /api/config не знает и ответит 404; тогда клиент ведёт себя как раньше, а не
// прячет половину интерфейса.
class ServerInfoController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool    loaded         READ loaded         NOTIFY changed)
    Q_PROPERTY(QString name           READ name           NOTIFY changed)
    Q_PROPERTY(bool    registration   READ registration   NOTIFY changed)
    Q_PROPERTY(bool    anonymousJoin  READ anonymousJoin  NOTIFY changed)
    // "open" | "account" | "off" — кто может завести разовую комнату.
    Q_PROPERTY(QString anonymousRooms READ anonymousRooms NOTIFY changed)
    Q_PROPERTY(int     minPasswordLen READ minPasswordLen NOTIFY changed)
    Q_PROPERTY(int     codeMinLen     READ codeMinLen     NOTIFY changed)
    Q_PROPERTY(int     maxAliases     READ maxAliases     NOTIFY changed)
    Q_PROPERTY(int     chatImageMaxKb READ chatImageMaxKb NOTIFY changed)
    // Ведёт ли сервер журнал с именами, кодами комнат и логинами.
    Q_PROPERTY(bool    logsNames      READ logsNames      NOTIFY changed)
    // Сборка сервера: короткий SHA и признак локальных правок. Показывать это
    // можно только со словами «сервер сообщает о себе»: значение приходит от
    // того же сервера, и проверить его снаружи нечем.
    Q_PROPERTY(QString versionCommit  READ versionCommit  NOTIFY changed)
    Q_PROPERTY(bool    versionModified READ versionModified NOTIFY changed)
public:
    explicit ServerInfoController(ApiClient* api, QObject* parent = nullptr);

    bool loaded() const { return m_loaded; }
    QString name() const { return m_name; }
    bool registration() const { return m_registration; }
    bool anonymousJoin() const { return m_anonymousJoin; }
    QString anonymousRooms() const { return m_anonymousRooms; }
    int minPasswordLen() const { return m_minPasswordLen; }
    int codeMinLen() const { return m_codeMinLen; }
    int maxAliases() const { return m_maxAliases; }
    int chatImageMaxKb() const { return m_chatImageMaxKb; }
    bool logsNames() const { return m_logsNames; }
    QString versionCommit() const { return m_versionCommit; }
    bool versionModified() const { return m_versionModified; }

    // Перечитать. Зовётся при старте и после смены адреса сервера — у другого
    // сервера и правила другие.
    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    void reset();   // вернуть разрешающие умолчания

    ApiClient* m_api;              // не владеем
    bool m_loaded = false;
    QString m_name = QStringLiteral("MeetUp");
    bool m_registration = true;
    bool m_anonymousJoin = true;
    QString m_anonymousRooms = QStringLiteral("open");
    int m_minPasswordLen = 8;
    int m_codeMinLen = 3;
    int m_maxAliases = 5;
    int m_chatImageMaxKb = 440;
    bool m_logsNames = false;
    QString m_versionCommit;
    bool m_versionModified = false;
};
