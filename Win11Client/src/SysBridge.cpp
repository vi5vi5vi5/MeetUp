#include "SysBridge.h"
#include "net/ApiClient.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QUrl>

static const char* kDefaultBase = "https://meetup.linkpc.net";

SysBridge::SysBridge(ApiClient* api, QObject* parent)
    : QObject(parent), m_api(api) {}

// Хост «как ввёл пользователь» — из строки адреса, НЕ через QUrl: с пустым
// IDN-whitelist (см. main.cpp) QUrl показывал бы xn----8sbwpsp.xn--p1ai
// вместо мит-ап.рф. Для сети ACE правильный, для глаз — юникод.
static QString prettyHost(const QString& base) {
    const int scheme = base.indexOf("://");
    QString h = (scheme >= 0) ? base.mid(scheme + 3) : base;
    const int slash = h.indexOf('/');
    if (slash >= 0) h = h.left(slash);
    return h;
}

QString SysBridge::host() const {
    return prettyHost(m_api->baseUrl());    // https://мит-ап.рф -> мит-ап.рф
}

// В поле показываем коротко: для https — только хост; для http (локальная
// разработка) — полный адрес, чтобы схема не потерялась при следующей правке.
QString SysBridge::serverAddress() const {
    const QString b = m_api->baseUrl();
    return b.startsWith("https://") ? prettyHost(b) : b;
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
