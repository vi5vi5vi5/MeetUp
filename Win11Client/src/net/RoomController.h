#pragma once
#include <QObject>
#include <QString>

class ApiClient;

// Логика лобби: создать комнату / проверить код по HTTP и сообщить «входим».
// Виден из QML как Rooms. Один на всё приложение (лобби на любой странице).
class RoomController : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool		busy		READ busy		NOTIFY busyChanged)
	Q_PROPERTY(QString	errorText	READ errorText	NOTIFY errorTextChanged)
public:
	explicit RoomController(ApiClient* api, QObject* parent = nullptr);

	bool busy() const { return m_busy; }
	QString errorText() const { return m_errorText; }

	// Проверить код (GET) и, если 200, войти. name — имя участника.
	Q_INVOKABLE void joinByCode(const QString& code, const QString& name);
	// Создать новую комнату (POST) и войти в неё.
	Q_INVOKABLE void createRoom(const QString& name);
	// Доверенный код (своя комната, история) — войти без проверки. Для входа в личную комнату сервер не просит ни пароль ни имя, сам по сессии пускает
	Q_INVOKABLE void enter(const QString& code, const QString& name);
	// Сбросить текст ошибки (например, при смене экрана).
	Q_INVOKABLE void clearError() { setError(""); }
signals:
	void busyChanged();
	void errorTextChanged();
	// «Комната готова — открывай конференцию с этим кодом и именем».
	void roomReady(const QString& code, const QString& name);
private:
	void setBusy(bool v);
	void setError(const QString& text);

	ApiClient* m_api;
	bool m_busy = false;
	QString m_errorText;
};