#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>

class ApiClient;
class AuthController;
class RoomController;
class SysBridge;

// Ссылка-приглашение как единственный ввод: «вставил — вошёл».
//
// Участники обмениваются готовыми адресами вида
// https://host/conference.html?room=КОД (иногда с ключом E2E во фрагменте,
// #k=…). Браузер такую ссылку просто открывает; десктопу нужно достать код,
// а если ссылка ведёт на ЧУЖОЙ сервер — ещё и переключиться туда, пустить
// гостем и после конференции вернуть прежний адрес вместе с сессией аккаунта.
// Вся эта хореография живёт здесь; навигацией по-прежнему занимается Main.qml
// по сигналам roomReady (RoomController) и anonEntryRequested (наш).
//
// В QML виден как "Link".
class LinkController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    // Ключ E2E из ссылки. Шифрования на десктопе пока нет (M5) — ключ храним
    // и показываем предупреждение, наружу он не уходит.
    Q_PROPERTY(QString pendingKey READ pendingKey NOTIFY pendingKeyChanged)
    // Идёт смена сервера: Main.qml на это время отдаёт навигацию нам, иначе
    // сброс профиля увёл бы на экран входа перед анонимным лобби.
    Q_PROPERTY(bool switching READ switching NOTIFY switchingChanged)
    // Адрес, на который надо вернуться после чужой конференции («» — некуда).
    Q_PROPERTY(QString homeServer READ homeServer NOTIFY homeServerChanged)
    // Имя для гейта в конференции: аккаунта на чужом сервере нет, но имя из
    // прежнего подставить можно — человеку останется нажать «Подключиться».
    Q_PROPERTY(QString suggestedName READ suggestedName NOTIFY suggestedNameChanged)
    // Почему мы вдруг на другом сервере («» — никуда не уходили).
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
public:
    LinkController(ApiClient* api, AuthController* auth, RoomController* rooms,
                   SysBridge* sys, QObject* parent = nullptr);

    QString errorText() const { return m_errorText; }
    QString pendingKey() const { return m_pendingKey; }
    bool switching() const { return m_switching; }
    QString homeServer() const { return m_homeServer; }
    QString suggestedName() const { return m_suggestedName; }
    QString notice() const { return m_notice; }

    // Разбор без побочных эффектов — им же пользуется open().
    // Ключи: ok(bool), code, key, base, sameServer(bool), error.
    Q_INVOKABLE QVariantMap parse(const QString& text) const;
    // Разобрать и действовать: войти, либо переключить сервер и позвать гостем.
    Q_INVOKABLE void open(const QString& text);
    // Конференция закрыта — вернуть прежний сервер, если мы уходили на чужой.
    Q_INVOKABLE void roomLeft();
    Q_INVOKABLE void clearError() { setError(""); }

signals:
    void errorTextChanged();
    void pendingKeyChanged();
    void switchingChanged();
    void homeServerChanged();
    void suggestedNameChanged();
    void noticeChanged();

private:
    void setError(const QString& text);
    void setPendingKey(const QString& key);
    void setSwitching(bool v);
    void setHomeServer(const QString& base);
    void setSuggestedName(const QString& name);
    void setNotice(const QString& text);
    // Открыть комнату: имя знаем — входим молча, не знаем — гейт в конференции
    // спросит его сам (как гейт веба на conference.html).
    void enterRoom(const QString& code);
    // Схема+хост+порт в сравнимом виде: хост в ACE и нижнем регистре, порт по
    // умолчанию отброшен. Без этого мит-ап.рф и xn--… считались бы разными.
    static QString normalizeBase(const QString& base);
    QString switchNotice() const;   // текст плашки «сервер переключён»

    ApiClient* m_api;          // не владеем
    AuthController* m_auth;    // не владеем
    RoomController* m_rooms;   // не владеем
    SysBridge* m_sys;          // не владеем: там же нормализация адреса

    QString m_errorText;
    QString m_pendingKey;
    QString m_suggestedName;
    QString m_notice;
    bool m_switching = false;
    QString m_homeServer;      // куда возвращаться после чужой конференции
    QString m_homeName;        // имя из аккаунта, который был открыт до ухода
    QString m_wantCode;        // код комнаты, ждущий ответа checkSession()
    bool m_restoring = false;  // checkSession() спрошен ради возврата домой
};
