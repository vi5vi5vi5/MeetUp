#include "LinkPreviews.h"
#include "ApiClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

namespace {
// Ссылок в кэше. Больше — выбрасываем всё разом: это не потеря, а повторный
// запрос, который сервер и так ответит из своего кэша.
constexpr int kMaxCached = 500;
}

LinkPreviews::LinkPreviews(ApiClient* api, QObject* parent)
    : QObject(parent), m_api(api) {}

QString LinkPreviews::strip(const QString& url) {
    QUrl u(url.trimmed());
    return u.adjusted(QUrl::RemoveFragment).toString(QUrl::FullyEncoded);
}

QVariantMap LinkPreviews::get(const QString& rawUrl) {
    // Другой сервер — другие ответы (и другой адрес картинок).
    if (m_api->baseUrl() != m_base) {
        m_base = m_api->baseUrl();
        m_cache.clear();
        m_inFlight.clear();
    }

    const QString key = strip(rawUrl);
    if (key.isEmpty())
        return {{"state", "none"}};
    const auto hit = m_cache.constFind(key);
    if (hit != m_cache.constEnd())
        return hit.value();
    if (m_inFlight.contains(key))
        return {{"state", "loading"}};

    if (m_cache.size() >= kMaxCached)
        m_cache.clear();
    m_inFlight.insert(key);

    const QString base = m_base;
    QNetworkReply* reply = m_api->get(
        "/api/unfurl?url=" + QString::fromLatin1(QUrl::toPercentEncoding(key)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, base]() {
        reply->deleteLater();
        if (base != m_base) return;          // пока ехал ответ, сменили сервер
        m_inFlight.remove(key);

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QString image = o.value("image").toString();
        QVariantMap v;
        if (status == 200 && (!o.value("title").toString().isEmpty() || !image.isEmpty())) {
            v = {
                {"state", "ready"},
                {"url", o.value("url").toString()},
                {"site", o.value("site").toString()},
                {"title", o.value("title").toString()},
                {"description", o.value("description").toString()},
                // Картинка — только с нашего сервера: путь начинается с «/».
                {"image", image.startsWith('/') ? base + image : QString()},
            };
        } else {
            v = {{"state", "none"}};
            // «Занято» — временно: через минуту сервер ответит иначе, а
            // сообщение может ещё долго висеть на экране.
            if (status == 503) {
                QTimer::singleShot(60000, this, [this, key, base]() {
                    if (base == m_base) m_cache.remove(key);
                });
            }
        }
        m_cache.insert(key, v);
        emit updated(key);
    });
    return {{"state", "loading"}};
}
