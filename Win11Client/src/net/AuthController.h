#pragma once
#include <QObject>
#include <QString>
#include <QUrl>

class ApiClient;
class QJsonObject;

class AuthController : public QObject {
	Q_OBJECT
	// Свойства, за которыми следит QML. NOTIFY-сигнал => QML сам обновится.
	Q_PROPERTY(bool		busy		READ busy		 NOTIFY busyChanged)
	Q_PROPERTY(QString	errorText	READ errorText	 NOTIFY errorTextChanged)
	Q_PROPERTY(QString	displayName	READ displayName NOTIFY displayNameChanged)
	// "login" назвать нельзя — уже есть метод login(); одноимённое свойство перекрыло бы его в QML.
	Q_PROPERTY(QString	userLogin	READ userLogin	 NOTIFY profileChanged)
	Q_PROPERTY(QString	avatarUrl	READ avatarUrl	 NOTIFY profileChanged)
public:
	explicit AuthController(ApiClient* api, QObject* parent = nullptr);

	bool busy() const { return m_busy; }
	QString errorText() const { return m_errorText; }
	QString displayName() const { return m_displayName; }
	QString userLogin() const { return m_login; }
	QString avatarUrl() const;   // "" если не вошли или аватарки нет (avatar_ver == 0)

	// Вызывается из QML: Auth.login("anna", "secret")
	Q_INVOKABLE void login(const QString& login, const QString& password);
	// Вызывается один раз при старте: авто-вход по живой сессии.
	Q_INVOKABLE void checkSession();
	// Auth.registerAccount(login, имя, пароль, повтор). Не "register" — это ключевое слово C++.
	Q_INVOKABLE void registerAccount(const QString& login, const QString& displayName,
	                                 const QString& password, const QString& password2);
	// Завершить сессию на сервере и почистить локальное состояние.
	Q_INVOKABLE void logout();
	// Смена отображаемого имени (PATCH /api/me).
	Q_INVOKABLE void updateDisplayName(const QString& name);
	// Аватарка: файл из FileDialog -> центр-кроп 256x256 JPEG -> POST /api/me/avatar.
	Q_INVOKABLE void uploadAvatar(const QUrl& file);
	Q_INVOKABLE void removeAvatar();
	// Сбросить текст ошибки (при переходах между экранами/модалками).
	Q_INVOKABLE void clearError() { setError(""); }

signals:
	void busyChanged();
	void errorTextChanged();
	void displayNameChanged();
	void profileChanged();    // сменились login/аватарка (id, версия)
	void profileSaved();      // имя сохранено — модалка выходит из режима правки
	void loggedIn();          // успех: QML переходит на HomeScreen
	void loggedOut();         // вышли: QML возвращается на LoginScreen

private:
	// Применить объект user из любого ответа сервера — одна точка правды о профиле.
	void applyUser(const QJsonObject& user);
	void setBusy(bool v);
	void setError(const QString& text);

	ApiClient* m_api;         // не владеем (передан снаружи)
	bool m_busy = false;
	QString m_errorText;
	QString m_displayName;
	qint64 m_userId = 0;
	QString m_login;
	int m_avatarVer = 0;
};
