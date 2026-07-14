#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonObject>

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

private:
	QNetworkRequest makeRequest(const QString& path) const;

	QNetworkAccessManager m_nam;          // «движок»; живёт вместе с ApiClient
	QString m_base = "https://meetup.linkpc.net";  // прод-сервер (Let's Encrypt)
};