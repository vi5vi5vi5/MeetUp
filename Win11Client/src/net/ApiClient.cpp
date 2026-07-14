#include "ApiClient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

ApiClient::ApiClient(QObject* parent) : QObject(parent) {
    // QNetworkAccessManager по умолчанию УЖЕ имеет cookie jar (QNetworkCookieJar)
    // и сам хранит/шлёт куки в пределах жизни m_nam. Отдельно создавать не нужно.
    // (Куки живут только в памяти — при перезапуске приложения сессия «забудется».
    // TODO: Сделать обёртку QNAM для хранения сессии в QSettings
}

QNetworkRequest ApiClient::makeRequest(const QString& path) const {
    QNetworkRequest req{ QUrl(m_base + path) };
    // Просим сервер отвечать JSON-ом (как это делает web: header "Accept").
    req.setRawHeader("Accept", "application/json");
    return req;
}

QNetworkReply* ApiClient::get(const QString& path) {
    return m_nam.get(makeRequest(path));
}

QNetworkReply* ApiClient::post(const QString& path, const QJsonObject& body) {
    QNetworkRequest req = makeRequest(path);
    // Тело — JSON. Обязательно сказать серверу тип содержимого.
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // QJsonObject -> компактный QByteArray (без пробелов/переносов).
    const QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return m_nam.post(req, data);
}