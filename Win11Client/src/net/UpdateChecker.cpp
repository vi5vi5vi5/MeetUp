#include "UpdateChecker.h"
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>

#ifndef UPDATE_REPO
#  define UPDATE_REPO ""      // форк без своего репозитория просто не проверяет
#endif

namespace {

// Клиент живёт открытым сутками — одной проверки на старте мало, но и чаще
// раза в шесть часов дёргать чужой API незачем.
const int kPollHours = 6;

// Тело релиза целиком в пилюлю не влезет, да и не должно: это ссылка на
// «что нового», а не сам список.
const int kNotesLimit = 1200;

// Куда качаем. Отдельная папка, а не голый %TEMP%: её целиком сносим перед
// каждой попыткой, и промахнуться мимо чужих файлов невозможно.
QString workDir() {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + "/MeetUp-update";
}

// Адрес, которому мы готовы отдать управление. GitHub редиректит скачивание на
// свой объектный сторедж, и следовать за редиректом можно — но только туда.
bool trustedHost(const QUrl& u) {
    if (u.scheme() != QLatin1String("https")) return false;
    const QString h = u.host().toLower();
    return h == QLatin1String("github.com")
        || h == QLatin1String("api.github.com")
        || h.endsWith(QLatin1String(".github.com"))
        || h.endsWith(QLatin1String(".githubusercontent.com"));
}

// Рекурсивно удалить папку, молча. Провал не важен: следующая попытка
// перезапишет, а мусор в %TEMP% чистит система.
void wipe(const QString& path) {
    QDir d(path);
    if (d.exists()) d.removeRecursively();
}

// Системный tar.exe по полному пути, а не «tar.exe» из PATH. Zip понимает
// именно он (bsdtar, есть в Windows 10/11 из коробки); GNU-шный tar, который
// приезжает вместе с Git for Windows и вполне может стоять в PATH раньше,
// на zip отвечает «This does not look like a tar archive».
QString systemTar() {
    const QString root = qEnvironmentVariable("SystemRoot", "C:/Windows");
    const QString path = QDir::fromNativeSeparators(root) + "/System32/tar.exe";
    return QFile::exists(path) ? path : QStringLiteral("tar.exe");
}

} // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)),
      m_poll(new QTimer(this)) {
    m_poll->setInterval(kPollHours * 60 * 60 * 1000);
    connect(m_poll, &QTimer::timeout, this, &UpdateChecker::check);
}

UpdateChecker::~UpdateChecker() = default;

QString UpdateChecker::stateName() const {
    switch (m_state) {
    case Checking:    return QStringLiteral("checking");
    case UpToDate:    return QStringLiteral("uptodate");
    case Available:   return QStringLiteral("available");
    case Downloading: return QStringLiteral("downloading");
    case Ready:       return QStringLiteral("ready");
    case Failed:      return QStringLiteral("failed");
    case Idle:        break;
    }
    return QStringLiteral("idle");
}

void UpdateChecker::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit changed();
}

void UpdateChecker::fail(const QString& text) {
    m_error = text;
    m_state = Failed;
    emit changed();
}

// ---------- версии ----------

// Отрезать «v» спереди мало: теги этого проекта выглядят как
// «Win11Client_1.05», и первое же число в такой строке — это «11» из «Win11».
// Поэтому ищем ПОСЛЕДНЮЮ группу вида «числа через точки»: в имени продукта
// точек нет, а в номере версии они есть.
QString UpdateChecker::versionFromTag(const QString& tag) {
    static const QRegularExpression dotted(R"((\d+(?:\.\d+)+))");
    QString last;
    auto it = dotted.globalMatch(tag);
    while (it.hasNext()) last = it.next().captured(1);
    if (!last.isEmpty()) return last;
    // Версия без точки вовсе («…_2»): берём число на самом конце.
    static const QRegularExpression tail(R"((\d+)\s*$)");
    const auto m = tail.match(tag);
    return m.hasMatch() ? m.captured(1) : QString();
}

int UpdateChecker::compareVersions(const QString& a, const QString& b) {
    auto parts = [](QString v) {
        v = v.trimmed();
        if (v.startsWith('v') || v.startsWith('V')) v.remove(0, 1);
        // Хвост вида «-beta.2» в счёт не идёт: у нас нумерация простая, а
        // сравнивать буквы с цифрами — верный способ ошибиться молча.
        const int dash = v.indexOf('-');
        if (dash >= 0) v.truncate(dash);
        QList<int> out;
        const auto chunks = v.split('.');
        for (const QString& c : chunks) out << c.toInt();
        return out;
    };
    const QList<int> x = parts(a), y = parts(b);
    for (int i = 0; i < qMax(x.size(), y.size()); ++i) {
        const int xi = i < x.size() ? x[i] : 0;
        const int yi = i < y.size() ? y[i] : 0;
        if (xi != yi) return xi < yi ? -1 : 1;
    }
    return 0;
}

// ---------- проверка ----------

void UpdateChecker::check() {
    const QString repo = QStringLiteral(UPDATE_REPO);
    if (repo.isEmpty()) return;                      // сборка без источника обновлений
    if (m_state == Checking || m_state == Downloading) return;
    // Уже скачано и ждёт перезапуска — незачем спрашивать снова.
    if (m_state == Ready) return;
    m_error.clear();
    m_poll->start();                                 // первая проверка заводит таймер

    QNetworkRequest req(QUrl("https://api.github.com/repos/" + repo + "/releases/latest"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    // GitHub отвечает 403 на запросы без User-Agent — это их документированное
    // требование, а не каприз.
    req.setRawHeader("User-Agent",
        QByteArray("MeetUp-Win11Client/") + QCoreApplication::applicationVersion().toUtf8());

    m_error.clear();
    setState(Checking);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        onCheckReply(reply);
        });
}

void UpdateChecker::onCheckReply(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        // Пилюля на этом состоянии не появляется, а вот кнопке в «О программе»
        // ответить надо: человек спросил и ждёт ответа. Молчать так же, как до
        // нажатия, — худший из вариантов.
        fail("Не удалось связаться с GitHub: " + reply->errorString());
        return;
    }

    const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = versionFromTag(o.value("tag_name").toString());
    if (tag.isEmpty()) { setState(UpToDate); return; }

    if (compareVersions(tag, QCoreApplication::applicationVersion()) <= 0) {
        setState(UpToDate);                          // у нас свежее или то же
        return;
    }

    // Ищем zip. Сначала тот, в имени которого есть «win» — в релизе может
    // лежать и архив исходников, и сборка под другую систему.
    QString url;
    qint64 size = 0;
    const QJsonArray assets = o.value("assets").toArray();
    for (const QJsonValue& v : assets) {
        const QJsonObject a = v.toObject();
        const QString name = a.value("name").toString();
        if (!name.endsWith(".zip", Qt::CaseInsensitive)) continue;
        const bool win = name.contains("win", Qt::CaseInsensitive);
        if (url.isEmpty() || win) {
            url = a.value("browser_download_url").toString();
            size = qint64(a.value("size").toDouble());
            if (win) break;
        }
    }
    if (url.isEmpty() || !trustedHost(QUrl(url))) {
        // Релиз есть, а собранного клиента в нём нет — обновляться нечем, и
        // это ровно то же самое, что «у вас последняя версия».
        setState(UpToDate);
        return;
    }

    m_latest = tag;
    m_assetUrl = url;
    m_assetSize = size;
    m_notes = o.value("body").toString().trimmed();
    if (m_notes.size() > kNotesLimit) m_notes = m_notes.left(kNotesLimit) + "…";
    setState(Available);
}

// ---------- скачивание ----------

void UpdateChecker::download() {
    if (m_state != Available && m_state != Failed) return;
    // Сорваться могло и на самой проверке — тогда о новой версии мы ещё не
    // знаем, и «попробовать снова» означает спросить заново, а не качать
    // неизвестно что.
    if (m_assetUrl.isEmpty()) { check(); return; }

    QString why;
    if (!canWriteToAppDir(&why)) { fail(why); return; }

    wipe(workDir());
    if (!QDir().mkpath(workDir())) {
        fail("Не удалось создать временную папку для загрузки.");
        return;
    }

    m_error.clear();
    m_progress = m_assetSize > 0 ? 0 : -1;
    setState(Downloading);
    emit progressChanged();

    QNetworkRequest req{QUrl(m_assetUrl)};
    req.setRawHeader("User-Agent",
        QByteArray("MeetUp-Win11Client/") + QCoreApplication::applicationVersion().toUtf8());
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::downloadProgress, this,
        [this](qint64 got, qint64 total) {
            const int p = total > 0 ? int(got * 100 / total) : -1;
            if (p == m_progress) return;
            m_progress = p;
            emit progressChanged();
        });
    // Редирект GitHub уводит на объектный сторедж — туда идти можно, в любое
    // другое место нельзя.
    connect(reply, &QNetworkReply::redirected, this, [reply](const QUrl& to) {
        if (!trustedHost(to)) reply->abort();
        });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        onDownloadReply(reply);
        });
}

void UpdateChecker::onDownloadReply(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        fail("Не удалось скачать обновление: " + reply->errorString());
        return;
    }

    const QByteArray body = reply->readAll();
    // Размер знаем заранее из ответа API — расхождение значит, что приехало
    // не то, что обещано, и распаковывать это незачем.
    if (m_assetSize > 0 && body.size() != m_assetSize) {
        fail("Файл обновления скачался не целиком.");
        return;
    }

    const QString zip = workDir() + "/update.zip";
    QFile f(zip);
    if (!f.open(QIODevice::WriteOnly) || f.write(body) != body.size()) {
        fail("Не удалось сохранить файл обновления.");
        return;
    }
    f.close();

    const QString dir = unpack(zip);
    if (dir.isEmpty()) return;                       // текст ошибки уже выставлен

    m_unpacked = dir;
    m_progress = 100;
    emit progressChanged();
    setState(Ready);
}

// Распаковка через системный tar.exe: он есть в Windows 10/11 из коробки
// (bsdtar, zip понимает), и это избавляет от ещё одной зависимости ради
// одной операции. Тем же приёмом пользуется scripts/fetch-deps.ps1.
QString UpdateChecker::unpack(const QString& zipPath) {
    const QString out = workDir() + "/unpacked";
    if (!QDir().mkpath(out)) { fail("Не удалось создать папку для распаковки."); return {}; }

    QProcess tar;
    tar.start(systemTar(), {"-xf", QDir::toNativeSeparators(zipPath),
                            "-C", QDir::toNativeSeparators(out)});
    if (!tar.waitForStarted(5000)) {
        fail("В системе не нашёлся tar.exe — распаковать обновление нечем.");
        return {};
    }
    if (!tar.waitForFinished(120000) || tar.exitCode() != 0) {
        fail("Архив обновления не распаковался.");
        return {};
    }

    // Внутри может быть как плоский набор файлов, так и одна папка с ними.
    // Ориентир — где лежит наш .exe.
    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    if (QFile::exists(out + "/" + exeName)) return out;

    const QStringList subs = QDir(out).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& s : subs)
        if (QFile::exists(out + "/" + s + "/" + exeName)) return out + "/" + s;

    fail("В архиве обновления нет " + exeName + ".");
    return {};
}

// ---------- установка ----------

bool UpdateChecker::canWriteToAppDir(QString* why) {
    const QString dir = QCoreApplication::applicationDirPath();
    QFile probe(dir + "/.meetup-write-test");
    if (probe.open(QIODevice::WriteOnly)) {
        probe.close();
        probe.remove();
        return true;
    }
    if (why)
        *why = "Нет прав на запись в папку программы. Перенесите MeetUp в свою "
               "папку (например, в «Загрузки» или в профиль пользователя) — "
               "обновление ставится без прав администратора.";
    return false;
}

bool UpdateChecker::installAndRestart(const QStringList& args) {
    if (m_state != Ready || m_unpacked.isEmpty()) return false;

    QString why;
    if (!canWriteToAppDir(&why)) { fail(why); return false; }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString stamp = QString::number(QDateTime::currentSecsSinceEpoch());

    QStringList rel;
    const QDir src(m_unpacked);
    QDirIterator it(m_unpacked, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); rel << src.relativeFilePath(it.filePath()); }
    if (rel.isEmpty()) { fail("В архиве обновления нет файлов."); return false; }

    // Подмена должна быть или целиком, или никак: наполовину обновлённая папка
    // — это неработающая программа без внятного способа починиться.
    QList<QPair<QString, QString>> renamed;   // куда клали -> куда отодвинули старое
    QStringList placed;
    auto rollback = [&] {
        for (const QString& p : placed) QFile::remove(p);
        for (int i = renamed.size() - 1; i >= 0; --i)
            QFile::rename(renamed[i].second, renamed[i].first);
    };

    for (const QString& r : rel) {
        const QString dst = appDir + "/" + r;
        QDir().mkpath(QFileInfo(dst).absolutePath());
        if (QFile::exists(dst)) {
            // Занятый файл (мы сами и наши библиотеки) удалить нельзя, а
            // переименовать — можно. Здесь всё и держится.
            const QString old = dst + ".old-" + stamp;
            QFile::remove(old);
            if (!QFile::rename(dst, old)) {
                rollback();
                fail("Не удалось освободить файл " + r + ".");
                return false;
            }
            renamed.append({dst, old});
        }
        if (!QFile::copy(src.filePath(r), dst)) {
            rollback();
            fail("Не удалось записать файл " + r + ".");
            return false;
        }
        placed << dst;
    }

    // Стартуем новый экземпляр и уходим. Хвосты «*.old-…» снимет он сам при
    // запуске — сейчас они ещё заняты нами.
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), args, appDir)) {
        rollback();
        fail("Обновление установлено, но перезапустить программу не удалось. "
             "Закройте её и откройте заново.");
        return false;
    }
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    return true;
}

void UpdateChecker::sweepOldFiles() {
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList doomed;
    QDirIterator it(appDir, {"*.old-*"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) doomed << it.next();
    // Не вышло — не беда: файл ещё занят уходящим экземпляром, снесём в
    // следующий раз. Именно поэтому в имени метка времени, а не фиксированный
    // суффикс: несколько поколений не мешают друг другу.
    for (const QString& p : doomed) QFile::remove(p);
    wipe(workDir());
}
