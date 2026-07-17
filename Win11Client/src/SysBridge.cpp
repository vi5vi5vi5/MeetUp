#include "SysBridge.h"
#include "net/ApiClient.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QUrl>

static const char* kDefaultBase = "https://meetup.linkpc.net";

SysBridge::SysBridge(ApiClient* api, QObject* parent)
    : QObject(parent), m_api(api) {}

QString SysBridge::host() const {
    return QUrl(m_api->baseUrl()).host();   // https://meetup.linkpc.net -> meetup.linkpc.net
}

// В поле показываем коротко: для https — только хост; для http (локальная
// разработка) — полный адрес, чтобы схема не потерялась при следующей правке.
QString SysBridge::serverAddress() const {
    const QUrl u(m_api->baseUrl());
    return u.scheme() == "https" ? u.host() : m_api->baseUrl();
}

void SysBridge::copyText(const QString& text) {
    QGuiApplication::clipboard()->setText(text);   // системный буфер обмена
}

// Ссылка, по которой гость попадёт в комнату из браузера, — как roomLinkFull()
// веба. Код экранируем — привычка из №2.
QString SysBridge::roomLink(const QString& code) const {
    return m_api->baseUrl() + "/conference.html?room="
        + QString::fromUtf8(QUrl::toPercentEncoding(code));
}

void SysBridge::setServer(const QString& input) {
    QString v = input.trimmed();
    while (v.endsWith('/')) v.chop(1);             // хвостовые «/» — долой

    QString base;
    if (v.isEmpty())
        base = kDefaultBase;                       // пусто — вернуть прод
    else if (v.startsWith("http://") || v.startsWith("https://"))
        base = v;                                  // схему указали — верим
    else
        base = "https://" + v;                     // без схемы — https (как прод)

    if (base == m_api->baseUrl()) return;
    m_api->setBaseUrl(base);                       // сохранится в QSettings
    emit serverChanged();                          // host/serverAddress пересчитаются
}
