#include "ApiClient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUrl>


ApiClient::ApiClient(QObject* parent) : QObject(parent) {
    // QNetworkAccessManager по умолчанию УЖЕ имеет cookie jar (QNetworkCookieJar)
    // и сам хранит/шлёт куки в пределах жизни m_nam. Но нам нужно его наебать чуток
    m_jar = new SessionCookieJar(this);
    m_nam.setCookieJar(m_jar);   // QNAM забирает владение; наш указатель — для чтения

    // Адрес сервера: сохранённый пользователем или прод по умолчанию.
    m_base = QSettings().value("serverBase", m_base).toString();
    // Сессия переживает перезапуск: подкидываем сохранённую куку этого сервера.
    m_jar->restoreFor(QUrl(m_base));
}

void ApiClient::setBaseUrl(const QString& base) {
    if (m_base == base) return;
    m_base = base;
    QSettings().setValue("serverBase", m_base);
    // Если на этом сервере когда-то входили — кука уже в QSettings, подхватим.
    m_jar->restoreFor(QUrl(m_base));
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

QNetworkReply* ApiClient::patch(const QString& path, const QJsonObject& body) {
    QNetworkRequest req = makeRequest(path);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    // У QNAM нет готового patch() — шлём запрос с произвольным глаголом.
    return m_nam.sendCustomRequest(req, "PATCH", data);
}

QNetworkReply* ApiClient::del(const QString& path) {
    return m_nam.deleteResource(makeRequest(path));   // DELETE без тела
}

// Для веб интерфеса нужен https и нормальный сертификат. Что бы не париться там стоит nginx как удобная костыль-обёртка бэкенда
// Так как сам бэкенд поднят в отдельном контейнере, он не знает про всю движуху и открывает ws порт на :9000
// nginx пересылает туда всё с wss://host.com/ws
// тут проверка на то, надо трафик кидать на wss://host/ws или на  ws://host:9000
QString ApiClient::wsUrl() const {
    QUrl u(m_base);                       // m_base = "https://meetup.linkpc.net"
    const bool tls = (u.scheme() == "https");
    // Кириллический домен (мит-ап.рф) обязан уехать в сокет в punycode
    // (xn--…): DNS, TLS SNI, заголовок Host и сверка сертификата — всё ASCII.
    // QNAM конвертирует сам (поэтому HTTP работал), а QWebSocket берёт хост
    // из URL как есть — юникод ломал хендшейк ещё до сервера.
    // Для обычных ASCII-доменов toAce возвращает то же самое.
    const QString host = QString::fromLatin1(QUrl::toAce(u.host()));
    // https -> wss://host/ws ; http -> ws://host:9000
    if (tls) return "wss://" + host + "/ws";
    return "ws://" + host + ":9000";
}

QString ApiClient::sessionToken() const {
    // Именно cookiesForUrl, а не все подряд: если пользователь ходил на разные
    // серверы, в jar лежат куки нескольких доменов — берём только куку ТЕКУЩЕГО.
    const QList<QNetworkCookie> cookies = m_jar->cookiesForUrl(QUrl(m_base));
    for (const QNetworkCookie& c : cookies)
        if (c.name() == "meetup_session")
            return QString::fromUtf8(c.value());
    return {};   // анонимной сессии нет — вернём пусто
}