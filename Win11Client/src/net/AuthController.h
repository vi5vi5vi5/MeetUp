#pragma once
#include <QObject>
#include <QString>

class ApiClient;

class AuthController : public QObject {
	Q_OBJECT
	// Свойства, за которыми следит QML. NOTIFY-сигнал => QML сам обновится.
	Q_PROPERTY(bool		busy		READ busy		 NOTIFY busyChanged)
	Q_PROPERTY(QString	errorText	READ errorText	 NOTIFY errorTextChanged)
	Q_PROPERTY(QString	displayName	READ displayName NOTIFY displayNameChanged)
public:
	explicit AuthController(ApiClient* api, QObject* parent = nullptr);

	bool busy() const { return m_busy; }
	QString errorText() const { return m_errorText; }
	QString displayName() const { return m_displayName; }

	// Вызывается из QML: Auth.login("anna", "secret")
	Q_INVOKABLE void login(const QString& login, const QString& password);
	// Вызывается один раз при старте: авто-вход по живой сессии.
	Q_INVOKABLE void checkSession();

signals:
	void busyChanged();
	void errorTextChanged();
	void displayNameChanged();
	void loggedIn();          // успех: QML переходит на HomeScreen

private:
	void setBusy(bool v);
	void setError(const QString& text);

	ApiClient* m_api;         // не владеем (передан снаружи)
	bool m_busy = false;
	QString m_errorText;
	QString m_displayName;

};