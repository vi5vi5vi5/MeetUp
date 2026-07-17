#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include "SessionCookieJar.h"

class QNetworkReply;

// Тонкая обёртка над QNetworkAccessManager: единый HTTP-клиент приложения
// с одной cookie jar (в ней живёт сессия meetup_session).
class ApiClient : public QObject {
	Q_OBJECT
public:
	explicit ApiClient(QObject* parent = nullptr);

	// GET <base>/<path>. Возвращает "ответ" — на его finished() подпишется вызвавший.
	QNetworkReply* get(const QString& path);

	// POST <base>/<path> с телом-объектом JSON (может быть пустым).
	QNetworkReply* post(const QString& path, const QJsonObject& body = {});

	// PATCH <base>/<path> с JSON-телом (частичное обновление ресурса).
	QNetworkReply* patch(const QString& path, const QJsonObject& body);

	// DELETE <base>/<path>. Имя "del" — потому что delete занято языком.
	QNetworkReply* del(const QString& path);

	QString wsUrl() const;

	QString sessionToken() const; // "" если не залогинен

	// База вида "https://meetup.linkpc.net" — для сборки абсолютных URL (аватарки).
	QString baseUrl() const { return m_base; }

	// Сменить сервер (нормализованный URL без хвостового «/»). Сохраняется в
	// QSettings и переживает перезапуск; сессия нового сервера подхватывается.
	void setBaseUrl(const QString& base);

private:
	QNetworkRequest makeRequest(const QString& path) const;

	QNetworkAccessManager m_nam;          // «движок»; живёт вместе с ApiClient
	SessionCookieJar* m_jar = nullptr;
	QString m_base = "https://meetup.linkpc.net";  // прод-сервер (Let's Encrypt)
};