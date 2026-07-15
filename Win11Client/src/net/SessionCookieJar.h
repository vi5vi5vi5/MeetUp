#pragma once
#include <QNetworkCookieJar>
#include <QNetworkCookie>

// Крысиная обёртка над Qt классом что бы дёргать куки сессии из внутреннего хранилища
// Браузер подкидывает во все запросы куку автоматически, а тут надо сделать ход конём что бы сервак сессию увидел
// TODO: Здесь можно сессию сохранять в QSettings для перезахода
class SessionCookieJar : public QNetworkCookieJar {
public:
    using QNetworkCookieJar::QNetworkCookieJar;
    QList<QNetworkCookie> all() const { return allCookies(); }   // раскрываем protected
};