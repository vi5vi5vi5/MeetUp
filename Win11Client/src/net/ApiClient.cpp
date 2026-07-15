#include "ApiClient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>


ApiClient::ApiClient(QObject* parent) : QObject(parent) {
    // QNetworkAccessManager по умолчанию УЖЕ имеет cookie jar (QNetworkCookieJar)
    // и сам хранит/шлёт куки в пределах жизни m_nam. Но нам нужно его наебать чуток
    m_jar = new SessionCookieJar(this);
    m_nam.setCookieJar(m_jar);   // QNAM забирает владение; наш указатель — для чтения
}

// Собираем QNetworkRequest: URL + заголовки, общие для всех запросов.
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

// Для веб интерфеса нужен https и нормальный сертификат. Что бы не париться там стоит nginx как удобная костыль-обёртка бэкенда
// Так как сам бэкенд поднят в отдельном контейнере, он не знает про всю движуху и открывает ws порт на :9000
// nginx пересылает туда всё с wss://host.com/ws
// тут проверка на то, надо трафик кидать на wss://host/ws или на  ws://host:9000
QString ApiClient::wsUrl() const {
    QUrl u(m_base);                       // m_base = "https://meetup.linkpc.net"
    const bool tls = (u.scheme() == "https");
    QString host = u.host();
    // https -> wss://host/ws ; http -> ws://host:9000
    if (tls) return "wss://" + host + "/ws";
    int port = u.port(80);
    return "ws://" + host + ":9000";
}

QString ApiClient::sessionToken() const {
    const QList<QNetworkCookie> cookies = m_jar->all();
    for (const QNetworkCookie& c : cookies)
        if (c.name() == "meetup_session")
            return QString::fromUtf8(c.value());
    return {};   // анонимной сессии нет — вернём пусто
}