#include "services/LinkPreviewService.h"

#include <QCryptographicHash>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTimer>

namespace {

constexpr int kMaxUrlLen = 2048;
constexpr int kMaxRedirects = 4;
constexpr qint64 kMaxHtmlBytes = 512 * 1024;
constexpr qint64 kMaxImageBytes = 1536 * 1024;
// Таймаут бездействия (Qt сам обрывает, если байты не идут) и общий — на
// случай сервера, который отдаёт по байту в секунду и так держит соединение.
constexpr int kIdleTimeoutMs = 6000;
constexpr int kTotalTimeoutMs = 8000;
constexpr int kMaxRunning = 4;
constexpr int kMaxPerMinute = 120;
constexpr int kMaxWaiting = 32;
constexpr qint64 kImageCacheBytes = 24 * 1024 * 1024;
constexpr int kMaxPages = 1000;
constexpr int kPageTtlS = 3600;
constexpr int kFailTtlS = 600;
constexpr int kUnreachableTtlS = 90;
constexpr int kMaxImageUrls = 4000;
constexpr int kMaxTitle = 200;
constexpr int kMaxDescription = 300;
constexpr int kMaxSite = 80;

// Честное имя: сайт видит, кто к нему пришёл и зачем. Чужим краулером
// (facebookexternalhit и т. п.) не представляемся.
const QByteArray kUserAgent =
    "Mozilla/5.0 (compatible; MeetUpLinkPreview/1.0; +https://github.com/vi5vi5vi5/MeetUp)";

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// Windows-1251 — кириллица старых сайтов. QStringDecoder без ICU знает только
// UTF-* и Latin-1, а такие страницы ещё встречаются; таблица — 128 символов.
const char16_t kCp1251High[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

// KOI8-R — вторая старая кириллица: её до сих пор отдают сайты эпохи
// «Мошкова» (lib.ru — проверено, заголовок приходит именно в ней).
const char16_t kKoi8rHigh[128] = {
    0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524,
    0x252C, 0x2534, 0x253C, 0x2580, 0x2584, 0x2588, 0x258C, 0x2590,
    0x2591, 0x2592, 0x2593, 0x2320, 0x25A0, 0x2219, 0x221A, 0x2248,
    0x2264, 0x2265, 0x00A0, 0x2321, 0x00B0, 0x00B2, 0x00B7, 0x00F7,
    0x2550, 0x2551, 0x2552, 0x0451, 0x2553, 0x2554, 0x2555, 0x2556,
    0x2557, 0x2558, 0x2559, 0x255A, 0x255B, 0x255C, 0x255D, 0x255E,
    0x255F, 0x2560, 0x2561, 0x0401, 0x2562, 0x2563, 0x2564, 0x2565,
    0x2566, 0x2567, 0x2568, 0x2569, 0x256A, 0x256B, 0x256C, 0x00A9,
    0x044E, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433,
    0x0445, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E,
    0x043F, 0x044F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432,
    0x044C, 0x044B, 0x0437, 0x0448, 0x044D, 0x0449, 0x0447, 0x044A,
    0x042E, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413,
    0x0425, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E,
    0x041F, 0x042F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0416, 0x0412,
    0x042C, 0x042B, 0x0417, 0x0428, 0x042D, 0x0429, 0x0427, 0x042A,
};

QString decodeHigh(const QByteArray &bytes, const char16_t (&high)[128])
{
    QString out;
    out.reserve(bytes.size());
    for (const char c : bytes) {
        const uchar b = uchar(c);
        out.append(b < 0x80 ? QChar(b) : QChar(high[b - 0x80]));
    }
    return out;
}

// Кодировка из заголовка Content-Type или из <meta> в начале документа.
QByteArray charsetOf(const QByteArray &contentType, const QByteArray &html)
{
    static const QRegularExpression rxHeader(
        QStringLiteral("charset\\s*=\\s*[\"']?([A-Za-z0-9_\\-:.]+)"),
        QRegularExpression::CaseInsensitiveOption);
    auto m = rxHeader.match(QString::fromLatin1(contentType));
    if (m.hasMatch())
        return m.captured(1).toLatin1().toLower();
    // Мета-тег с кодировкой обязан стоять в первых 1024 байтах (HTML5), но
    // встречается и дальше — смотрим чуть шире.
    m = rxHeader.match(QString::fromLatin1(html.left(4096)));
    if (m.hasMatch())
        return m.captured(1).toLatin1().toLower();
    return QByteArrayLiteral("utf-8");
}

QString decodeHtml(const QByteArray &bytes, const QByteArray &charset)
{
    if (charset == "windows-1251" || charset == "cp1251" || charset == "win-1251")
        return decodeHigh(bytes, kCp1251High);
    if (charset == "koi8-r" || charset == "koi8r" || charset == "koi8")
        return decodeHigh(bytes, kKoi8rHigh);
    if (charset == "iso-8859-1" || charset == "latin1" || charset == "us-ascii")
        return QString::fromLatin1(bytes);
    QStringDecoder dec(charset.constData());
    if (dec.isValid()) {
        const QString s = dec(bytes);
        return s;
    }
    return QString::fromUtf8(bytes);
}

// Сущности HTML в заголовках: числовые и самые частые именованные. Остальные
// остаются как есть — лучше «&foo;» в заголовке, чем выдуманный символ.
QString decodeEntities(const QString &in)
{
    static const QHash<QString, QChar> named{
        {QStringLiteral("amp"), u'&'},     {QStringLiteral("lt"), u'<'},
        {QStringLiteral("gt"), u'>'},      {QStringLiteral("quot"), u'"'},
        {QStringLiteral("apos"), u'\''},   {QStringLiteral("nbsp"), QChar(0x00A0)},
        {QStringLiteral("mdash"), QChar(0x2014)}, {QStringLiteral("ndash"), QChar(0x2013)},
        {QStringLiteral("laquo"), QChar(0x00AB)}, {QStringLiteral("raquo"), QChar(0x00BB)},
        {QStringLiteral("hellip"), QChar(0x2026)}, {QStringLiteral("copy"), QChar(0x00A9)},
        {QStringLiteral("reg"), QChar(0x00AE)},   {QStringLiteral("trade"), QChar(0x2122)},
        {QStringLiteral("lsquo"), QChar(0x2018)}, {QStringLiteral("rsquo"), QChar(0x2019)},
        {QStringLiteral("ldquo"), QChar(0x201C)}, {QStringLiteral("rdquo"), QChar(0x201D)},
        {QStringLiteral("bdquo"), QChar(0x201E)}, {QStringLiteral("middot"), QChar(0x00B7)},
        {QStringLiteral("bull"), QChar(0x2022)},  {QStringLiteral("times"), QChar(0x00D7)},
    };
    static const QRegularExpression rx(QStringLiteral("&(#[0-9]{1,7}|#[xX][0-9a-fA-F]{1,6}|[a-zA-Z]{2,8});"));
    QString out;
    int last = 0;
    auto it = rx.globalMatch(in);
    while (it.hasNext()) {
        const auto m = it.next();
        out += QStringView(in).mid(last, m.capturedStart() - last);
        const QString e = m.captured(1);
        bool ok = false;
        if (e.startsWith(QLatin1Char('#'))) {
            const uint cp = (e.size() > 1 && (e.at(1) == u'x' || e.at(1) == u'X'))
                                ? e.mid(2).toUInt(&ok, 16) : e.mid(1).toUInt(&ok, 10);
            if (ok && cp > 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF))
                out += QString::fromUcs4(reinterpret_cast<const char32_t *>(&cp), 1);
            else
                out += m.captured(0);
        } else if (named.contains(e)) {
            out += named.value(e);
        } else {
            out += m.captured(0);
        }
        last = m.capturedEnd();
    }
    out += QStringView(in).mid(last);
    return out;
}

// Текст для показа: сущности раскрыты, пробелы и переводы строк сжаты, без
// управляющих символов, обрезан по длине.
QString cleanText(const QString &raw, int maxLen)
{
    QString s = decodeEntities(raw);
    QString out;
    out.reserve(s.size());
    bool space = false;
    for (const QChar c : s) {
        if (c.isSpace() || c == QChar(0x00A0)) {
            space = true;
            continue;
        }
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format)
            continue;
        if (space && !out.isEmpty())
            out += QLatin1Char(' ');
        space = false;
        out += c;
    }
    if (out.size() > maxLen)
        out = out.left(maxLen - 1).trimmed() + QChar(0x2026);
    return out;
}

// Атрибуты тега: name="..." | name='...' | name=bare.
QHash<QString, QString> attributesOf(const QString &tag)
{
    static const QRegularExpression rx(QStringLiteral(
        "([a-zA-Z_:.\\-]+)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))"));
    QHash<QString, QString> out;
    auto it = rx.globalMatch(tag);
    while (it.hasNext()) {
        const auto m = it.next();
        // Значение — из той группы, что сработала: в кавычках, в апострофах
        // или без них (capturedStart у несработавшей группы — -1).
        const int group = m.capturedStart(2) >= 0 ? 2 : (m.capturedStart(3) >= 0 ? 3 : 4);
        out.insert(m.captured(1).toLower(), m.captured(group));
    }
    return out;
}

bool isRedirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

QByteArray bareMime(const QByteArray &contentType)
{
    const int semi = contentType.indexOf(';');
    return (semi >= 0 ? contentType.left(semi) : contentType).trimmed().toLower();
}

bool isImageMime(const QByteArray &mime)
{
    return mime == "image/jpeg" || mime == "image/png" || mime == "image/gif"
        || mime == "image/webp";
}

} // namespace

// ---------------------------------------------------------------- состояния

struct LinkPreviewService::Fetch
{
    QUrl url;                    // текущий адрес (меняется на переадресациях)
    bool image = false;          // ждём картинку, а не страницу
    int redirects = 0;
    QNetworkReply *reply = nullptr;
    QByteArray data;
    QByteArray contentType;      // как пришёл, с charset
    QString error;               // пусто — успех
    bool stoppedEarly = false;   // сами оборвали, дочитав </head>
    std::function<void(Fetch *)> done;
};

struct LinkPreviewService::Job
{
    QString key;
    QUrl url;
    QList<Done> waiters;
};

// ---------------------------------------------------------------- сервис

LinkPreviewService::LinkPreviewService(bool enabled, QObject *parent)
    : QObject(parent), m_enabled(enabled)
{
    m_nam = new QNetworkAccessManager(this);
    // Ни системного прокси, ни кэша: сервер ходит сам и ничего не копит.
    m_nam->setProxy(QNetworkProxy::NoProxy);
    m_pumpTimer = new QTimer(this);
    m_pumpTimer->setInterval(500);
    connect(m_pumpTimer, &QTimer::timeout, this, &LinkPreviewService::pump);
}

LinkPreviewService::~LinkPreviewService() = default;

bool LinkPreviewService::acceptableUrl(const QUrl &url)
{
    if (!url.isValid() || url.isRelative())
        return false;
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;
    if (url.host().isEmpty() || !url.userInfo().isEmpty())
        return false;
    // Порты — только «веб»: ссылка вида http://host:6379 — это не страница,
    // а попытка достучаться до чужого сервиса нашими руками.
    const int port = url.port(-1);
    if (port != -1 && port != 80 && port != 443 && port != 8080 && port != 8443)
        return false;
    return url.toString(QUrl::FullyEncoded).size() <= kMaxUrlLen;
}

QString LinkPreviewService::cacheKey(const QUrl &url)
{
    QUrl u = url;
    u.setFragment(QString());
    u.setHost(u.host().toLower());
    return u.toString(QUrl::FullyEncoded);
}

bool LinkPreviewService::isPublicAddress(const QHostAddress &addr)
{
    QHostAddress a = addr;
    bool v4 = false;
    const quint32 ip = a.toIPv4Address(&v4);   // в том числе ::ffff:a.b.c.d
    if (v4)
        a = QHostAddress(ip);

    // isPrivateUse() здесь нет намеренно: он появился в Qt 6.6, а образ
    // собирается на Qt из Debian bookworm (6.4). Частные сети IPv4 закрыты
    // явными диапазонами ниже, IPv6 — isUniqueLocalUnicast/isSiteLocal.
    if (a.isNull() || a.isLoopback() || a.isLinkLocal() || a.isMulticast()
        || a.isBroadcast() || a.isUniqueLocalUnicast() || a.isSiteLocal()
        || !a.isGlobal())
        return false;

    if (v4) {
        const auto in = [ip](quint32 net, int bits) {
            const quint32 mask = bits == 0 ? 0 : 0xFFFFFFFFu << (32 - bits);
            return (ip & mask) == (net & mask);
        };
        // Явно — то, что Qt может считать «глобальным» (isGlobal() у Qt
        // включает и частные сети RFC 1918) или чего в его списке нет.
        if (in(0x00000000, 8)       // 0.0.0.0/8
            || in(0x0A000000, 8)    // 10/8
            || in(0x64400000, 10)   // 100.64/10 — CGNAT
            || in(0x7F000000, 8)    // 127/8
            || in(0xA9FE0000, 16)   // 169.254/16 — link-local, облачный metadata
            || in(0xAC100000, 12)   // 172.16/12
            || in(0xC0000000, 24)   // 192.0.0/24
            || in(0xC0000200, 24)   // 192.0.2/24 — документация
            || in(0xC0A80000, 16)   // 192.168/16
            || in(0xC6120000, 15)   // 198.18/15 — бенчмарки
            || in(0xC6336400, 24)   // 198.51.100/24
            || in(0xCB007100, 24)   // 203.0.113/24
            || in(0xE0000000, 3))   // 224/3 — multicast и резерв
            return false;
    }
    return true;
}

void LinkPreviewService::preview(const QString &rawUrl, const Done &done)
{
    if (!m_enabled) {
        done(LinkPreviewResult::fail(QStringLiteral("disabled")));
        return;
    }
    QUrl url(rawUrl.trimmed(), QUrl::StrictMode);
    if (!acceptableUrl(url)) {
        done(LinkPreviewResult::fail(QStringLiteral("invalid_url")));
        return;
    }
    url.setFragment(QString());
    const QString key = cacheKey(url);

    const auto hit = m_pages.constFind(key);
    if (hit != m_pages.constEnd() && hit->until > QDateTime::currentDateTimeUtc()) {
        done(hit->result);
        return;
    }
    if (Job *job = m_jobs.value(key)) {
        job->waiters.append(done);
        return;
    }
    auto *job = new Job;
    job->key = key;
    job->url = url;
    job->waiters.append(done);
    m_jobs.insert(key, job);
    startJob(job);
}

QString LinkPreviewService::imageIdOf(const QUrl &url)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex().left(24));
}

void LinkPreviewService::image(const QString &id, const ImageDone &done)
{
    auto it = m_images.find(id);
    if (it != m_images.end()) {
        it->lastUsedMs = nowMs();
        done(true, it->data, it->mime);
        return;
    }
    const auto url = m_imageUrls.constFind(id);
    if (url == m_imageUrls.constEnd()) {
        done(false, {}, {});
        return;
    }
    // Ту же картинку просят сразу все участники, увидевшие сообщение, — в
    // очередь к первому, наружу один раз.
    auto waiting = m_imageWaiters.find(id);
    if (waiting != m_imageWaiters.end()) {
        waiting->append(done);
        return;
    }
    m_imageWaiters.insert(id, {done});

    auto *f = new Fetch;
    f->url = url.value();
    f->image = true;
    f->done = [this, id](Fetch *fetch) {
        const QByteArray mime = bareMime(fetch->contentType);
        const bool ok = fetch->error.isEmpty() && isImageMime(mime) && !fetch->data.isEmpty();
        if (ok)
            putImage(id, fetch->data, mime);
        else if (fetch->error != QLatin1String("busy"))
            m_imageUrls.remove(id);       // не вышло — больше не пытаемся
        const QList<ImageDone> waiters = m_imageWaiters.take(id);
        for (const ImageDone &d : waiters)
            d(ok, fetch->data, mime);
    };
    startFetch(f);
}

void LinkPreviewService::putImage(const QString &id, const QByteArray &data,
                                  const QByteArray &mime)
{
    auto it = m_images.find(id);
    if (it != m_images.end())
        m_imageBytes -= it->data.size();
    m_images.insert(id, CachedImage{data, mime, nowMs()});
    m_imageBytes += data.size();
    // Вытесняем самые давно нужные, пока не влезем в потолок.
    while (m_imageBytes > kImageCacheBytes && m_images.size() > 1) {
        auto oldest = m_images.begin();
        for (auto i = m_images.begin(); i != m_images.end(); ++i)
            if (i->lastUsedMs < oldest->lastUsedMs)
                oldest = i;
        m_imageBytes -= oldest->data.size();
        m_images.erase(oldest);
    }
}

// ---------------------------------------------------------------- работа

void LinkPreviewService::startJob(Job *job)
{
    auto *f = new Fetch;
    f->url = job->url;
    f->done = [this, job](Fetch *fetch) { onPageDone(job, fetch); };
    startFetch(f);
}

void LinkPreviewService::onPageDone(Job *job, Fetch *f)
{
    if (!f->error.isEmpty()) {
        finishJob(job, LinkPreviewResult::fail(f->error));
        return;
    }

    LinkPreviewResult result;
    result.ok = true;
    LinkPreview &p = result.preview;
    p.url = f->url.toString();
    QString host = f->url.host();
    if (host.startsWith(QLatin1String("www.")))
        host = host.mid(4);

    // Ссылка прямо на картинку: показываем её саму, подпись — имя файла.
    const QByteArray mime = bareMime(f->contentType);
    if (isImageMime(mime)) {
        const QString id = imageIdOf(f->url);
        putImage(id, f->data, mime);
        p.site = host;
        p.title = cleanText(f->url.fileName(), kMaxTitle);
        p.imageId = id;
        finishJob(job, result);
        return;
    }

    QUrl imageUrl;
    if (!parseHead(f->data, f->contentType, f->url, &p, &imageUrl)) {
        finishJob(job, LinkPreviewResult::fail(QStringLiteral("no_preview")));
        return;
    }
    if (p.site.isEmpty())
        p.site = host;

    // Картинку не ждём: запоминаем её адрес под ключом, а скачаем, когда
    // клиент её попросит (см. image). Не скачается — карточка останется без
    // неё, заголовка с описанием достаточно.
    if (imageUrl.isValid() && acceptableUrl(imageUrl)) {
        imageUrl.setFragment(QString());
        const QString id = imageIdOf(imageUrl);
        if (m_imageUrls.size() >= kMaxImageUrls && !m_imageUrls.contains(id))
            m_imageUrls.clear();          // память важнее: ключи старых карточек
        m_imageUrls.insert(id, imageUrl);
        p.imageId = id;
    }
    finishJob(job, result);
}

void LinkPreviewService::finishJob(Job *job, const LinkPreviewResult &result)
{
    // «Занято» не кэшируем: через минуту ответ будет другим.
    if (result.error != QLatin1String("busy")) {
        if (m_pages.size() >= kMaxPages) {
            // Сначала выбрасываем протухшее; не помогло — что попалось.
            const QDateTime now = QDateTime::currentDateTimeUtc();
            for (auto it = m_pages.begin(); it != m_pages.end();)
                it = it->until <= now ? m_pages.erase(it) : std::next(it);
            while (m_pages.size() >= kMaxPages)
                m_pages.erase(m_pages.begin());
        }
        const int ttl = result.ok ? kPageTtlS
                      : result.error == QLatin1String("unreachable") ? kUnreachableTtlS
                                                                     : kFailTtlS;
        m_pages.insert(job->key, CachedPage{result, QDateTime::currentDateTimeUtc().addSecs(ttl)});
    }
    const QList<Done> waiters = job->waiters;
    m_jobs.remove(job->key);
    delete job;
    for (const Done &d : waiters)
        d(result);
}

// ---------------------------------------------------------------- походы

bool LinkPreviewService::takeBudget()
{
    const qint64 now = nowMs();
    while (!m_recentStarts.isEmpty() && now - m_recentStarts.first() > 60000)
        m_recentStarts.removeFirst();
    if (m_recentStarts.size() >= kMaxPerMinute)
        return false;
    m_recentStarts.append(now);
    return true;
}

void LinkPreviewService::startFetch(Fetch *f)
{
    if (m_running >= kMaxRunning) {
        if (m_waiting.size() >= kMaxWaiting) {
            f->error = QStringLiteral("busy");
            auto done = f->done;
            done(f);
            delete f;
            return;
        }
        m_waiting.append(f);
        m_pumpTimer->start();
        return;
    }
    if (!takeBudget()) {
        f->error = QStringLiteral("busy");
        auto done = f->done;
        done(f);
        delete f;
        return;
    }
    ++m_running;
    resolveAndGo(f);
}

void LinkPreviewService::pump()
{
    while (!m_waiting.isEmpty() && m_running < kMaxRunning)
        startFetch(m_waiting.takeFirst());
    if (m_waiting.isEmpty())
        m_pumpTimer->stop();
}

void LinkPreviewService::resolveAndGo(Fetch *f)
{
    const auto finish = [this](Fetch *fetch, const QString &error) {
        fetch->error = error;
        --m_running;
        auto done = fetch->done;
        done(fetch);
        delete fetch;
        pump();
    };

    const QString host = f->url.host();
    QHostAddress literal;
    if (literal.setAddress(host)) {
        if (!isPublicAddress(literal)) {
            finish(f, QStringLiteral("forbidden_address"));
            return;
        }
        sendRequest(f, literal);
        return;
    }

    QHostInfo::lookupHost(host, this, [this, f, finish](const QHostInfo &info) {
        const QList<QHostAddress> addrs = info.addresses();
        if (info.error() != QHostInfo::NoError || addrs.isEmpty()) {
            finish(f, QStringLiteral("unreachable"));
            return;
        }
        // ВСЕ адреса имени обязаны быть публичными: имя, у которого среди
        // адресов есть внутренний, — это либо ошибка, либо приманка.
        for (const QHostAddress &a : addrs) {
            if (!isPublicAddress(a)) {
                finish(f, QStringLiteral("forbidden_address"));
                return;
            }
        }
        QHostAddress pick = addrs.first();
        for (const QHostAddress &a : addrs) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                pick = a;
                break;
            }
        }
        sendRequest(f, pick);
    });
}

void LinkPreviewService::sendRequest(Fetch *f, const QHostAddress &addr)
{
    // Идём на проверенный адрес, а имя отдаём в Host и в проверку
    // сертификата (она же — SNI). Так второго разрешения имени, которое могло
    // бы ответить внутренним адресом, просто нет.
    QUrl target = f->url;
    target.setHost(addr.toString());
    const QString host = QString::fromLatin1(QUrl::toAce(f->url.host()));
    const int port = f->url.port(-1);

    QNetworkRequest req(target);
    req.setRawHeader("Host", (port == -1 ? host : host + QLatin1Char(':') + QString::number(port)).toLatin1());
    req.setPeerVerifyName(host);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // HTTP/2 берёт :authority из адреса, то есть из IP, а не из Host —
    // сайт за общим фронтом не понял бы, какой сайт у него просят.
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    req.setTransferTimeout(kIdleTimeoutMs);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setRawHeader("Accept", f->image
        ? QByteArrayLiteral("image/webp,image/png,image/jpeg,image/gif;q=0.9,*/*;q=0.1")
        : QByteArrayLiteral("text/html,application/xhtml+xml;q=0.9,image/*;q=0.5,*/*;q=0.1"));
    req.setRawHeader("Accept-Language", "ru,en;q=0.8");

    f->data.clear();
    f->contentType.clear();
    f->stoppedEarly = false;
    QNetworkReply *reply = m_nam->get(req);
    f->reply = reply;

    QTimer::singleShot(kTotalTimeoutMs, reply, [reply] { reply->abort(); });

    connect(reply, &QNetworkReply::readyRead, this, [this, f, reply] {
        if (f->reply != reply)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (isRedirect(status)) {
            reply->readAll();            // тело переадресации не нужно
            return;
        }
        if (f->contentType.isEmpty())
            f->contentType = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
        const QByteArray mime = bareMime(f->contentType);
        const bool wantImage = f->image || isImageMime(mime);
        // Не то, что ждали — дальше не читаем. Без Content-Type вовсе — считаем
        // страницей: так отвечают простые самописные сайты.
        if (!wantImage && !mime.isEmpty() && mime != "text/html"
            && mime != "application/xhtml+xml") {
            f->error = QStringLiteral("not_html");
            reply->abort();
            return;
        }
        if (f->image && !isImageMime(mime)) {
            f->error = QStringLiteral("not_html");
            reply->abort();
            return;
        }
        const int before = int(f->data.size());
        f->data += reply->readAll();
        const qint64 cap = wantImage ? kMaxImageBytes : kMaxHtmlBytes;
        if (f->data.size() > cap) {
            if (wantImage) {
                f->error = QStringLiteral("not_html");   // слишком большая картинка
            } else {
                f->data.truncate(int(cap));
                f->stoppedEarly = true;                  // разберём, что успели
            }
            reply->abort();
            return;
        }
        // Всё нужное — в <head>: дочитали — дальше не качаем. Ищем только в
        // свежем куске (с запасом на тег, разрезанный границей кусков), иначе
        // каждый кусок перебирал бы всё прочитанное заново.
        if (!wantImage
            && f->data.mid(qMax(0, before - 8)).toLower().contains("</head")) {
            f->stoppedEarly = true;
            reply->abort();
            return;
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, f] { onReply(f); });
}

void LinkPreviewService::onReply(Fetch *f)
{
    QNetworkReply *reply = f->reply;
    f->reply = nullptr;
    reply->deleteLater();

    const auto finish = [this, f](const QString &error) {
        f->error = error;
        --m_running;
        auto done = f->done;
        done(f);
        delete f;
        pump();
    };

    if (!f->error.isEmpty()) {
        finish(f->error);
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (isRedirect(status)) {
        const QUrl loc = reply->header(QNetworkRequest::LocationHeader).toUrl();
        const QUrl next = f->url.resolved(loc);
        if (!loc.isValid() || ++f->redirects > kMaxRedirects || !acceptableUrl(next)) {
            finish(QStringLiteral("unreachable"));
            return;
        }
        f->url = next;
        f->url.setFragment(QString());
        resolveAndGo(f);                 // место в лимите остаётся за нами
        return;
    }

    if (reply->error() != QNetworkReply::NoError && !f->stoppedEarly) {
        finish(QStringLiteral("unreachable"));
        return;
    }
    if (status < 200 || status >= 300) {
        finish(QStringLiteral("unreachable"));
        return;
    }
    if (!f->stoppedEarly)
        f->data += reply->readAll();
    if (f->contentType.isEmpty())
        f->contentType = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
    if (f->data.isEmpty()) {
        finish(QStringLiteral("unreachable"));
        return;
    }
    finish(QString());
}

// ---------------------------------------------------------------- разбор

bool LinkPreviewService::parseHead(const QByteArray &html, const QByteArray &contentType,
                                   const QUrl &base, LinkPreview *out, QUrl *imageUrl)
{
    QByteArray bytes = html;
    const int headEnd = bytes.toLower().indexOf("</head");
    if (headEnd > 0)
        bytes.truncate(headEnd);
    const QString doc = decodeHtml(bytes, charsetOf(contentType, html));

    QHash<QString, QString> meta;        // первое значение каждого ключа
    static const QRegularExpression rxMeta(QStringLiteral("<meta\\b[^>]*>"),
                                           QRegularExpression::CaseInsensitiveOption);
    auto it = rxMeta.globalMatch(doc);
    while (it.hasNext()) {
        const auto attrs = attributesOf(it.next().captured(0));
        const QString key = (attrs.contains(QStringLiteral("property"))
                                 ? attrs.value(QStringLiteral("property"))
                                 : attrs.value(QStringLiteral("name"))).toLower();
        if (key.isEmpty() || !attrs.contains(QStringLiteral("content")) || meta.contains(key))
            continue;
        meta.insert(key, attrs.value(QStringLiteral("content")));
    }

    const auto first = [&meta](std::initializer_list<const char *> keys) {
        for (const char *k : keys) {
            const QString v = meta.value(QLatin1String(k)).trimmed();
            if (!v.isEmpty())
                return v;
        }
        return QString();
    };

    QString title = first({"og:title", "twitter:title"});
    if (title.isEmpty()) {
        static const QRegularExpression rxTitle(QStringLiteral("<title[^>]*>(.*?)</title>"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        const auto m = rxTitle.match(doc);
        if (m.hasMatch())
            title = m.captured(1);
    }
    out->title = cleanText(title, kMaxTitle);
    out->description = cleanText(first({"og:description", "twitter:description", "description"}),
                                 kMaxDescription);
    out->site = cleanText(first({"og:site_name", "application-name"}), kMaxSite);

    QString img = first({"og:image:secure_url", "og:image", "og:image:url",
                         "twitter:image", "twitter:image:src"});
    if (img.isEmpty()) {
        static const QRegularExpression rxLink(QStringLiteral("<link\\b[^>]*>"),
                                               QRegularExpression::CaseInsensitiveOption);
        auto li = rxLink.globalMatch(doc);
        while (li.hasNext()) {
            const auto attrs = attributesOf(li.next().captured(0));
            if (attrs.value(QStringLiteral("rel")).toLower() == QLatin1String("image_src")) {
                img = attrs.value(QStringLiteral("href"));
                break;
            }
        }
    }
    if (!img.isEmpty())
        *imageUrl = base.resolved(QUrl(decodeEntities(img.trimmed())));

    return !out->title.isEmpty() || !out->description.isEmpty() || !img.isEmpty();
}
