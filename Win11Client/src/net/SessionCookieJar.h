#pragma once
#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <QSettings>
#include <QDateTime>
#include <QUrl>

// Крысиная обёртка над Qt классом что бы дёргать куки сессии из внутреннего хранилища.
// Браузер подкидывает во все запросы куку автоматически, а тут надо сделать ход конём
// что бы сервак сессию увидел.
//
// Плюс постоянство: кука meetup_session дублируется в QSettings (ключ
// "session/<host>" — на каждый сервер своя), поэтому сессия переживает
// перезапуск приложения. Logout стирает и копию: сервер шлёт Set-Cookie с
// Max-Age=0, мы видим «просроченную» куку и удаляем ключ.
class SessionCookieJar : public QNetworkCookieJar {
public:
    using QNetworkCookieJar::QNetworkCookieJar;

    QList<QNetworkCookie> all() const { return allCookies(); }   // раскрываем protected

    // Перехват ответов сервера: любая перемена meetup_session отражается в QSettings.
    bool setCookiesFromUrl(const QList<QNetworkCookie>& list, const QUrl& url) override {
        const bool ok = QNetworkCookieJar::setCookiesFromUrl(list, url);
        for (const QNetworkCookie& c : list) {
            if (c.name() != "meetup_session") continue;
            QSettings s;
            const QString key = "session/" + url.host();
            // Пустая или просроченная (logout: Max-Age=0) — забываем и мы.
            const bool dead = c.value().isEmpty()
                || (c.expirationDate().isValid()
                    && c.expirationDate() <= QDateTime::currentDateTimeUtc());
            if (dead) s.remove(key);
            else      s.setValue(key, QString::fromUtf8(c.value()));
        }
        return ok;
    }

    // Восстановить сохранённую сессию для этого сервера (при старте и при смене
    // адреса). Зовём БАЗОВУЮ setCookiesFromUrl — чтобы не пересохранять то же самое.
    void restoreFor(const QUrl& baseUrl) {
        const QString token =
            QSettings().value("session/" + baseUrl.host()).toString();
        if (token.isEmpty()) return;
        QNetworkCookie c("meetup_session", token.toUtf8());
        c.setPath("/");   // домен подставится из baseUrl
        QNetworkCookieJar::setCookiesFromUrl({ c }, baseUrl);
    }
};
