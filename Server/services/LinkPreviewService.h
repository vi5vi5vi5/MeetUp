#pragma once

#include <functional>

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Предпросмотр ссылки из чата: заголовок, описание и картинка страницы.
struct LinkPreview
{
    QString url;            // куда в итоге привела ссылка (после переадресаций)
    QString site;           // og:site_name, иначе хост без www
    QString title;
    QString description;
    QString imageId;        // ключ картинки в кэше (см. image); пусто — без картинки
};

// Результат-или-код-ошибки, как у остальных сервисов. Коды:
//   invalid_url      — не http(s), с логином внутри, на странном порту, слишком длинная
//   forbidden_address — адрес частной сети, локальный и прочие «не наружу» (SSRF)
//   unreachable      — не отвечает, не резолвится, обрыв, таймаут
//   not_html         — по ссылке не страница и не картинка
//   no_preview       — страница есть, а показать нечего (ни заголовка, ни картинки)
//   busy             — превышен общий лимит походов наружу; повторить позже
//   disabled         — предпросмотр выключен в конфиге
struct LinkPreviewResult
{
    bool ok = false;
    QString error;
    LinkPreview preview;

    static LinkPreviewResult fail(const QString &code)
    {
        LinkPreviewResult r;
        r.error = code;
        return r;
    }
};

// Сервер сходит по ссылке вместо клиента. Причин две:
//   * браузер прочитать чужую страницу не может (CORS) — без сервера в вебе
//     предпросмотра не было бы вовсе;
//   * если по ссылке пойдёт каждый участник сам, тот, кто её прислал, узнает
//     адреса всех. Сейчас участники не видят IP друг друга — всё медиа идёт
//     через сервер, — и предпросмотр «в лоб» это сломал бы.
//
// Сервер при этом становится тем, кто ходит по чужим ссылкам, — то есть
// мишенью SSRF. Поэтому:
//   * только http и https, без логина в адресе, на портах 80/443/8080/8443;
//   * имя резолвится заранее, и все его адреса обязаны быть публичными:
//     частные сети, localhost, link-local (там у облаков живёт metadata),
//     CGNAT и прочее «не наружу» — отказ;
//   * соединение идёт на ПРОВЕРЕННЫЙ IP, а не на имя: иначе DNS мог бы между
//     проверкой и запросом ответить уже внутренним адресом (DNS rebinding).
//     Имя едет в Host и в SNI, сертификат сверяется по нему же;
//   * переадресации — вручную, не больше четырёх, каждая проверяется заново;
//   * страница читается только до </head> и не больше 512 КБ, картинка — не
//     больше 1,5 МБ, на всё — 6 секунд;
//   * общий потолок: 4 похода одновременно и 120 в минуту. Лимит по адресу
//     клиента не работает: за nginx все приходят с одного адреса.
//
// Ничего не пишется на диск и в журнал: адреса ссылок — переписка людей.
// Кэш в памяти: страницы — час (отказы — 10 минут), картинки — до 24 МБ.
class LinkPreviewService : public QObject
{
    Q_OBJECT
public:
    using Done = std::function<void(const LinkPreviewResult &)>;

    explicit LinkPreviewService(bool enabled, QObject *parent = nullptr);
    ~LinkPreviewService() override;

    bool enabled() const { return m_enabled; }

    // Предпросмотр ссылки. done зовётся один раз — сразу (кэш, отказ) или
    // когда страница дочитана.
    void preview(const QString &url, const Done &done);

    // Картинка из кэша по ключу из LinkPreview::imageId. false — её нет
    // (вытеснена или ключ чужой).
    bool image(const QString &id, QByteArray *data, QByteArray *mime);

    // ---- для проверок ----
    // Адрес, на который ходить можно: публичный unicast, не частная сеть.
    static bool isPublicAddress(const QHostAddress &addr);
    // Разбор <head>: мета-теги og:/twitter:, <title>, description.
    // Возвращает true, если нашлось хоть что-то показать.
    static bool parseHead(const QByteArray &html, const QByteArray &contentType,
                          const QUrl &base, LinkPreview *out, QUrl *imageUrl);

private:
    struct Fetch;            // один поход наружу (страница или картинка)
    struct Job;              // предпросмотр одной ссылки: страница, потом картинка

    struct CachedPage {
        LinkPreviewResult result;
        QDateTime until;
    };
    struct CachedImage {
        QByteArray data;
        QByteArray mime;
        qint64 lastUsedMs = 0;
    };

    // Валидный ли адрес для похода: схема, порт, длина, без userinfo.
    static bool acceptableUrl(const QUrl &url);
    static QString cacheKey(const QUrl &url);

    void startJob(Job *job);
    void onPageDone(Job *job, Fetch *fetch);
    void onImageDone(Job *job, Fetch *fetch);
    void finishJob(Job *job, const LinkPreviewResult &result);

    // Поход с проверкой адреса и ручными переадресациями. Колбэк получает
    // заполненный fetch (ok, data, mime, finalUrl или error).
    void startFetch(Fetch *fetch);
    void resolveAndGo(Fetch *fetch);
    void sendRequest(Fetch *fetch, const QHostAddress &addr);
    void onReply(Fetch *fetch);

    // Лимит походов наружу: true — можно идти сейчас.
    bool takeBudget();
    void pump();             // запустить ждущих, если освободилось место

    void putImage(const QString &id, const QByteArray &data, const QByteArray &mime);

    bool m_enabled = true;
    QNetworkAccessManager *m_nam = nullptr;
    QHash<QString, CachedPage> m_pages;
    QHash<QString, CachedImage> m_images;
    qint64 m_imageBytes = 0;
    // Ссылки в работе: второй запрос той же ссылки встаёт в очередь к первому,
    // а не идёт наружу ещё раз (сообщение видят все участники разом).
    QHash<QString, Job *> m_jobs;
    QList<Fetch *> m_waiting;        // ждут свободного места
    int m_running = 0;               // походов в полёте
    QList<qint64> m_recentStarts;    // метки начала походов за последнюю минуту
    QTimer *m_pumpTimer = nullptr;
};
