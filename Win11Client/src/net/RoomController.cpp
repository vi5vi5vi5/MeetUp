#include "RoomController.h"
#include "ApiClient.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

RoomController::RoomController(ApiClient* api, QObject* parent): QObject(parent), m_api(api) {}

void RoomController::setBusy(bool v) {
	if (m_busy == v) return;
	m_busy = v;
	emit busyChanged();
}
void RoomController::setError(const QString& text) {
	if (m_errorText == text) return;
	m_errorText = text;
	emit errorTextChanged();
}

// Доверенный код: сразу входим, без обращения к серверу как к себе домой.
void RoomController::enter(const QString& code, const QString& name) {
	const QString c = code.trimmed();
	if (c.isEmpty()) { setError("Пустой код комнаты."); return; }
	setError("");
	emit roomReady(c, name.trimmed());
}

void RoomController::joinByCode(const QString& code, const QString& name) {
	const QString c = code.trimmed();
	if (name.trimmed().isEmpty()) { setError("Введите ваше имя."); return; }
	if (c.isEmpty()) { setError("Введите код комнаты."); return; }
	setError("");
	setBusy(true);

	// Код едет в ПУТИ URL, а не в теле. Его надо экранировать: пробел, кириллица
	// и т.п. в пути недопустимы. Наши коды — латиница/цифры/`-`/`_`, но привычку
	// экранировать заводим сразу (веб делает encodeURIComponent).

	const QString path = "/api/rooms/" + QString::fromUtf8(
		QUrl::toPercentEncoding(c));
	QNetworkReply* reply = m_api->get(path);

	connect(reply, &QNetworkReply::finished, this, [this, reply, c, name]() {
		setBusy(false);
		reply->deleteLater();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status == 200) {
			emit roomReady(c, name.trimmed());
			return;
		}
		if (status == 404) setError("Комната не найдена. Проверьте код.");
		else if (status == 0) setError("Сервер недоступен. Попробуйте позже.");
		else setError("Не удалось проверить комнату (" + QString::number(status) + ").");
		});

}

void RoomController::createRoom(const QString& name) {
	// Веб не требует имя для создания на главной, но в анонимном лобби требует
	// («Введите имя, чтобы создать трансляцию»). Требуем всегда — так проще и
	// логичнее: без имени в конференции делать нечего.
	if (name.trimmed().isEmpty()) { setError("Введите ваше имя."); return; }
	setError("");
	setBusy(true);

	QNetworkReply* reply = m_api->post("/api/rooms");   // тело пустое

	connect(reply, &QNetworkReply::finished, this, [this, reply, name]() {
		setBusy(false);
		reply->deleteLater();
		const int status =
			reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QJsonObject obj =
			QJsonDocument::fromJson(reply->readAll()).object();

		// Успех: 200 и в теле есть непустой "room".
		const QString code = obj.value("room").toString();
		if (status == 200 && !code.isEmpty()) {
			emit roomReady(code, name.trimmed());
			return;
		}
		if (status == 0) setError("Сервер недоступен. Попробуйте позже.");
		else setError("Не удалось создать комнату. Попробуйте позже.");
		});
}