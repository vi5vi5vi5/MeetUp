#pragma once
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantMap>

class ApiClient;

// Предпросмотр ссылок из чата (QML: Previews).
//
// По ссылке ходит СЕРВЕР MeetUp (GET /api/unfurl), а не этот клиент: иначе
// каждый, кто пришлёт ссылку на свой сайт, узнал бы адреса всех участников с
// десктопом, а сейчас они друг другу не видны — медиа идёт через сервер.
// Картинка тоже приходит с сервера (/api/unfurl/image/…), по той же причине.
//
// Часть адреса после «#» на сервер не уходит никогда: там живут ключи —
// #k= у приглашения MeetUp с ключом E2E, ключ тома у ссылки Ferry, — а для
// предпросмотра страницы она не нужна вовсе.
//
// Когда показывать карточки (сервер умеет, человек не выключил, комната не
// под E2E или он разрешил и там), решает QML: сюда приходят только те ссылки,
// которые уже можно спрашивать.
class LinkPreviews : public QObject {
    Q_OBJECT
public:
    explicit LinkPreviews(ApiClient* api, QObject* parent = nullptr);

    // Состояние предпросмотра ссылки:
    //   { state: "loading" }                    — спросили сервер, ждём;
    //   { state: "ready", url, site, title,
    //     description, image }                  — показать (image — полный
    //                                              адрес картинки или "");
    //   { state: "none" }                       — показывать нечего.
    // Первый вызов для ссылки запускает запрос; о готовности скажет updated.
    Q_INVOKABLE QVariantMap get(const QString& url);

signals:
    // Ответ по ссылке пришёл. Ключ — адрес без «#…»; карточки, которые ещё
    // ждут, просто спрашивают get() заново — ответ уже в кэше.
    void updated(const QString& key);

private:
    // Ключ кэша и то, что уходит серверу: без «#…».
    static QString strip(const QString& url);

    ApiClient* m_api;                  // не владеем
    QString m_base;                    // сервер, для которого собран кэш
    QHash<QString, QVariantMap> m_cache;
    QSet<QString> m_inFlight;
};
