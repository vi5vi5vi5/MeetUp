#pragma once

// Ferry в MeetUp целиком — то, что QML видит как Ferry.
//
// Опознаёт ссылки Ferry в чате (на любом домене: по форме адреса, а не по
// имени хоста), раздаёт карточкам их FerryTransfer — один на раздачу на всё
// время работы приложения, — держит поток, в котором идут приёмы, и
// настройки: куда класть принятое, раздавать ли его другим, каким релеям
// можно показывать свой адрес без спроса.
//
// Доверенные релеи — релей из настроек сервера MeetUp (ferry_relay) и те,
// которым человек доверился сам («Доверять этому релею дальше»). Остальным
// карточка не пишет ничего, пока её не попросят: спросить релей о раздаче
// значит показать ему свой адрес, а ссылку в чат мог бросить кто угодно.

#include <QHash>
#include <QObject>
#include <QStringList>

class FerryTransfer;
class QNetworkAccessManager;
class QThread;
class ServerInfoController;
class SignalingClient;

class FerryHub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString downloadDir READ downloadDir WRITE setDownloadDir NOTIFY settingsChanged)
    // Раздавать ли скачанное тем, кто придёт позже: "call" — пока я в звонке
    // (умолчание), "app" — пока открыто приложение, "off" — не раздавать.
    Q_PROPERTY(QString seedMode READ seedMode WRITE setSeedMode NOTIFY settingsChanged)
    Q_PROPERTY(QStringList trustedRelays READ trustedRelays NOTIFY settingsChanged)
    // Сколько раздач сейчас качается или раздаётся — для вопроса при выходе.
    Q_PROPERTY(int active READ active NOTIFY activeChanged)

public:
    FerryHub(ServerInfoController *server, SignalingClient *conf, QObject *parent = nullptr);
    ~FerryHub() override;

    QString downloadDir() const;
    void setDownloadDir(const QString &dir);
    QString seedMode() const;
    void setSeedMode(const QString &mode);
    QStringList trustedRelays() const;
    int active() const { return m_active; }

    // Похоже ли на ссылку раздачи: https://<любой хост>/t/<id из 22 знаков>,
    // с ключом после # или без.
    Q_INVOKABLE bool isFerryLink(const QString &url) const;
    // Карточке — её раздача. Одна и та же для всех сообщений с этим id.
    Q_INVOKABLE QObject *transfer(const QString &url);
    Q_INVOKABLE bool isTrusted(const QString &origin) const;
    Q_INVOKABLE void trust(const QString &origin);
    Q_INVOKABLE void forgetTrusted();
    // Показать файл в проводнике, выделив его.
    Q_INVOKABLE void reveal(const QString &path);
    // «Загрузки / том» вместо полного пути — для карточки.
    Q_INVOKABLE QString displayPath(const QString &path) const;

    QNetworkAccessManager *nam() const { return m_nam; }
    QThread *ioThread();
    QString myName() const;
    QString stateDir() const;
    bool seedByDefault() const;

    // Приложение закрывается: остановить приёмы, сохранив карты.
    void shutdown();

signals:
    void settingsChanged();
    void activeChanged();
    void transferFinished(const QString &name);

private:
    void recount();
    void onLeftCall();
    static QString normOrigin(const QString &origin);

    ServerInfoController *m_server;   // не владеем
    SignalingClient *m_conf;          // не владеем
    QNetworkAccessManager *m_nam;
    QThread *m_io = nullptr;
    QHash<QString, FerryTransfer *> m_transfers;
    int m_active = 0;
};
