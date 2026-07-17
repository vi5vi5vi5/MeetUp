#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>

class ApiClient;
class QJsonObject;

// Личная комната владельца (/api/me/room): состояние для главной и операции
// создать / настроить / завершить / удалить, плюс alias-ссылки (/aliases).
// Виден из QML как MyRoom.
class PersonalRoomController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool        loaded    READ loaded    NOTIFY roomChanged)
    Q_PROPERTY(bool        exists    READ exists    NOTIFY roomChanged)
    Q_PROPERTY(QVariantMap room      READ room      NOTIFY roomChanged)
    Q_PROPERTY(bool        busy      READ busy      NOTIFY busyChanged)
    Q_PROPERTY(QString     errorText READ errorText NOTIFY errorTextChanged)
    // Alias-ссылки — своё состояние и своя ошибка (обе секции модалки видны разом).
    Q_PROPERTY(QVariantList aliases       READ aliases       NOTIFY aliasesChanged)
    Q_PROPERTY(bool         aliasesLoaded READ aliasesLoaded NOTIFY aliasesChanged)
    Q_PROPERTY(QString      aliasError    READ aliasError    NOTIFY aliasErrorChanged)
public:
    explicit PersonalRoomController(ApiClient* api, QObject* parent = nullptr);

    bool loaded() const { return m_loaded; }
    bool exists() const { return m_exists; }
    QVariantMap room() const { return m_room; }
    bool busy() const { return m_busy; }
    QString errorText() const { return m_errorText; }
    QVariantList aliases() const { return m_aliases; }
    bool aliasesLoaded() const { return m_aliasesLoaded; }
    QString aliasError() const { return m_aliasError; }

    Q_INVOKABLE void refresh();                          // GET: обновить состояние
    Q_INVOKABLE void create(const QString& code, const QString& title,
                            const QString& password);    // POST: новая комната
    Q_INVOKABLE void change(const QVariantMap& patch);   // PATCH: только изменившееся
    Q_INVOKABLE void remove();                           // DELETE: удалить насовсем
    Q_INVOKABLE void closeRoom();                        // POST close: завершить эфир
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
    void applyRoom(const QJsonObject& room);
    void setBusy(bool v);
    void setError(const QString& t);
    void setAliasError(const QString& t);
    static QString errText(const QString& code);
    static QString aliasErrText(const QString& code);

    ApiClient* m_api;          // не владеем
    bool m_loaded = false;     // хоть один ответ (200/404) получен
    bool m_exists = false;
    QVariantMap m_room;
    bool m_busy = false;
    QString m_errorText;
    QVariantList m_aliases;
    bool m_aliasesLoaded = false;
    QString m_aliasError;
};
