#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>

class QJsonArray;

class ApiClient;
class QJsonObject;

// Личные комнаты владельца (/api/me/rooms): состояние для главной и операции
// создать / настроить / завершить / удалить, плюс alias-ссылки (/aliases).
// Виден из QML как MyRoom.
//
// Комнат у человека может быть несколько (сколько — говорит сервер в поле
// max). Но правят их по одной: модалка настроек работает ровно с одной
// комнатой, и ссылки-приглашения принадлежат ей же. Поэтому
// «текущая комната» живёт здесь, а не в QML: select(id) переключает, а
// change/remove и все операции со ссылками относятся к текущей. Так вызовы из
// QML не таскают за собой номер, который и так один на экран. Исключение —
// closeRoom(id): «Завершить» есть у каждой комнаты списка, и переключать ради
// неё текущую значило бы перестраивать список прямо под курсором.
class PersonalRoomController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool        loaded    READ loaded    NOTIFY roomChanged)
    Q_PROPERTY(bool        exists    READ exists    NOTIFY roomChanged)
    // Текущая комната: её показывает карточка и правит модалка настроек.
    Q_PROPERTY(QVariantMap room      READ room      NOTIFY roomChanged)
    // Все комнаты владельца и потолок с сервера — для списка и счётчика «2 из 3».
    Q_PROPERTY(QVariantList rooms    READ rooms     NOTIFY roomChanged)
    Q_PROPERTY(int         maxRooms  READ maxRooms  NOTIFY roomChanged)
    Q_PROPERTY(int         currentId READ currentId NOTIFY roomChanged)
    Q_PROPERTY(bool        busy      READ busy      NOTIFY busyChanged)
    Q_PROPERTY(QString     errorText READ errorText NOTIFY errorTextChanged)
    // Alias-ссылки — своё состояние и своя ошибка (обе секции модалки видны разом).
    Q_PROPERTY(QVariantList aliases       READ aliases       NOTIFY aliasesChanged)
    Q_PROPERTY(bool         aliasesLoaded READ aliasesLoaded NOTIFY aliasesChanged)
    Q_PROPERTY(QString      aliasError    READ aliasError    NOTIFY aliasErrorChanged)
public:
    explicit PersonalRoomController(ApiClient* api, QObject* parent = nullptr);

    bool loaded() const { return m_loaded; }
    bool exists() const { return !m_rooms.isEmpty(); }
    QVariantMap room() const { return m_room; }
    QVariantList rooms() const { return m_rooms; }
    int maxRooms() const { return m_maxRooms; }
    int currentId() const { return m_currentId; }
    bool busy() const { return m_busy; }
    QString errorText() const { return m_errorText; }
    QVariantList aliases() const { return m_aliases; }
    bool aliasesLoaded() const { return m_aliasesLoaded; }
    QString aliasError() const { return m_aliasError; }

    Q_INVOKABLE void refresh();                          // GET: обновить состояние
    // Переключить текущую комнату. Номер чужой или несуществующей игнорируем.
    Q_INVOKABLE void select(int roomId);
    Q_INVOKABLE void create(const QString& code, const QString& title,
                            const QString& password);    // POST: новая комната
    Q_INVOKABLE void change(const QVariantMap& patch);   // PATCH: только изменившееся
    Q_INVOKABLE void remove();                           // DELETE: удалить насовсем
    // POST close: завершить эфир. По умолчанию — у текущей комнаты.
    Q_INVOKABLE void closeRoom(int roomId = -1);
    Q_INVOKABLE void reset();                            // при выходе из аккаунта
    Q_INVOKABLE QString slugify(const QString& raw) const; // код по правилам сервера
    Q_INVOKABLE void clearError() { setError(""); }

    Q_INVOKABLE void loadAliases();                      // при открытии модалки настроек
    Q_INVOKABLE void createAlias(const QString& password, const QString& usesText,
                                 const QString& loginsText);
    Q_INVOKABLE void toggleAlias(int id, bool enabled);
    Q_INVOKABLE void deleteAlias(int id);

signals:
    void roomChanged();
    void busyChanged();
    void errorTextChanged();
    void created();     // POST прошёл — модалка создания закрывается
    void saved();       // PATCH прошёл — режимы правки закрываются
    void removed();     // DELETE прошёл — модалка настроек закрывается
    void aliasesChanged();
    void aliasErrorChanged();
    void aliasCreated();    // форма новой ссылки закрывается

private:
    void applyRooms(const QJsonArray& rooms, int max);
    // Сделать текущей комнату с этим номером; -1 — выбрать саму подходящую
    // (ту, где сейчас люди, иначе первую).
    void setCurrent(int roomId);
    // Адрес текущей комнаты: "/api/me/rooms/<id>" плюс хвост.
    QString roomPath(const QString& tail = QString()) const;
    void setBusy(bool v);
    void setError(const QString& t);
    void setAliasError(const QString& t);
    static QString errText(const QString& code);
    static QString aliasErrText(const QString& code);

    ApiClient* m_api;          // не владеем
    bool m_loaded = false;     // хоть один ответ получен
    QVariantMap m_room;        // текущая комната
    QVariantList m_rooms;      // все комнаты владельца, в порядке создания
    int m_maxRooms = 1;
    int m_currentId = -1;
    bool m_busy = false;
    QString m_errorText;
    QVariantList m_aliases;
    bool m_aliasesLoaded = false;
    QString m_aliasError;
};
