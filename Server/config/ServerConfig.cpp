#include "config/ServerConfig.h"

#include <optional>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QTextStream>

#ifndef MEETUP_COMMIT
#define MEETUP_COMMIT "unknown"
#endif
#ifndef MEETUP_MODIFIED
#define MEETUP_MODIFIED 0
#endif
#ifndef MEETUP_BUILD_TIME
#define MEETUP_BUILD_TIME ""
#endif

namespace {

const char *kFileName = "meetup.conf";

// Образец конфига: всё закомментировано, значения равны умолчаниям. Человек
// раскомментирует строку — и только тогда что-то меняется. Так файл заодно
// работает справочником: какие ключи вообще есть и что они значат, видно, не
// заглядывая в README.
const char *kSample = R"conf(# MeetUp — настройки сервера.
#
# Файл создан сервером при первом запуске. Он лежит в примонтированной папке
# mount/, поэтому переживает и пересборку образа, и `git pull`.
#
# Все значения ниже равны умолчаниям — раскомментируйте строку, чтобы
# поменять. После правки перезапустите сервер:
#     docker compose restart app
#
# Любой ключ можно переопределить переменной окружения вида
# MEETUP_<СЕКЦИЯ>_<КЛЮЧ>, например MEETUP_WEB_ENABLED=false. Окружение
# сильнее файла.

[server]
# Имя инстанса. Его видят на странице входа и в десктопном клиенте после
# ввода адреса сервера.
#name = MeetUp

# Адрес, по которому сервер виден снаружи, — для ссылок-приглашений.
# Сам сервер стоит за прокси и своего внешнего имени не знает.
#public_url = https://meetup.example.com

[web]
# Раздавать ли веб-клиент. false — на сервере не остаётся ни одной страницы,
# которую можно открыть браузером: только JSON API и WebSocket-релей.
# Десктопные клиенты при этом работают полностью.
#enabled = true

[log]
# Что писать в журнал (docker logs):
#   off     — молчание, включая ошибки
#   errors  — строки старта, предупреждения и ошибки        (умолчание)
#   normal  — плюс события: подключения, вход в комнату, регистрации
#
# Умолчание здесь — единственное, которое отличается от поведения старых
# версий, и это сознательно: события содержат имена участников, коды комнат
# и логины, а MeetUp обещает, что журнала подключений нет.
#level = errors
)conf";

// Разобранный файл: "секция/ключ" -> значение. Чтение одноразовое, поэтому
// прочитанные ключи отсюда вынимаются — что осталось, то в конфиге лишнее.
class Ini
{
public:
    // Возвращает false, только если файл есть, но не читается.
    bool read(const QString &path, QStringList *warnings)
    {
        QFile f(path);
        if (!f.exists())
            return true;
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            warnings->append(QStringLiteral("не удалось прочитать %1").arg(path));
            return false;
        }

        QTextStream in(&f);
        QString section;
        int lineNo = 0;
        while (!in.atEnd()) {
            ++lineNo;
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
                || line.startsWith(QLatin1Char(';')))
                continue;

            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                section = line.mid(1, line.size() - 2).trimmed().toLower();
                continue;
            }

            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0) {
                warnings->append(QStringLiteral("%1:%2 — строка не похожа на «ключ = значение»")
                                     .arg(path).arg(lineNo));
                continue;
            }
            const QString key = line.left(eq).trimmed().toLower();
            const QString value = line.mid(eq + 1).trimmed();
            m_values.insert(section + QLatin1Char('/') + key, value);
        }
        return true;
    }

    // Значение ключа: сначала окружение, потом файл. Найденный ключ считается
    // разобранным и из остатка исчезает.
    std::optional<QString> take(const char *section, const char *key)
    {
        const QByteArray env = qgetenv(QByteArrayLiteral("MEETUP_") + QByteArray(section).toUpper()
                                       + '_' + QByteArray(key).toUpper());
        const QString path = QLatin1String(section) + QLatin1Char('/') + QLatin1String(key);
        const auto it = m_values.find(path);
        std::optional<QString> fromFile;
        if (it != m_values.end()) {
            fromFile = it.value();
            m_values.erase(it);
        }
        if (!env.isNull())
            return QString::fromUtf8(env).trimmed();
        return fromFile;
    }

    // Ключи, которых сервер не знает: почти всегда это опечатка, и молчать о
    // ней нельзя — человек будет уверен, что настройка применилась.
    QStringList leftovers() const
    {
        QStringList out = m_values.keys();
        out.sort();
        return out;
    }

private:
    QHash<QString, QString> m_values;
};

bool parseBool(const QString &raw, bool fallback, const char *where, QStringList *warnings)
{
    const QString v = raw.trimmed().toLower();
    if (v == QLatin1String("true") || v == QLatin1String("yes")
        || v == QLatin1String("on") || v == QLatin1String("1"))
        return true;
    if (v == QLatin1String("false") || v == QLatin1String("no")
        || v == QLatin1String("off") || v == QLatin1String("0"))
        return false;
    warnings->append(QStringLiteral("%1: «%2» — не да и не нет, оставляю %3")
                         .arg(QLatin1String(where), raw,
                              fallback ? QStringLiteral("true") : QStringLiteral("false")));
    return fallback;
}

} // namespace

ServerConfig ServerConfig::load(const QString &dataDir)
{
    ServerConfig cfg;
    cfg.path = QDir(dataDir).filePath(QLatin1String(kFileName));

    // Файла нет — кладём образец. Это делается один раз в жизни сервера и
    // избавляет человека от поиска «а где вообще настройки».
    if (!QFile::exists(cfg.path)) {
        QSaveFile sample(cfg.path);
        if (sample.open(QIODevice::WriteOnly | QIODevice::Text)) {
            sample.write(kSample);
            cfg.createdNow = sample.commit();
        }
        if (!cfg.createdNow)
            cfg.warnings.append(QStringLiteral("не удалось создать %1").arg(cfg.path));
    }

    Ini ini;
    ini.read(cfg.path, &cfg.warnings);

    if (const auto v = ini.take("server", "name"))
        cfg.name = v->isEmpty() ? cfg.name : *v;
    if (const auto v = ini.take("server", "public_url"))
        cfg.publicUrl = *v;

    if (const auto v = ini.take("web", "enabled"))
        cfg.webEnabled = parseBool(*v, cfg.webEnabled, "web.enabled", &cfg.warnings);

    if (const auto v = ini.take("log", "level")) {
        bool ok = false;
        const Log::Level level = Log::levelFromString(*v, &ok);
        if (ok)
            cfg.logLevel = level;
        else
            cfg.warnings.append(QStringLiteral("log.level: «%1» — не off/errors/normal, "
                                               "оставляю errors").arg(*v));
    }

    for (const QString &key : ini.leftovers())
        cfg.warnings.append(QStringLiteral("%1: ключ «%2» серверу неизвестен — опечатка?")
                                .arg(cfg.path, key));
    return cfg;
}

QString ServerConfig::buildCommit()
{
    const QString commit = QString::fromLatin1(MEETUP_COMMIT).trimmed();
    return commit.isEmpty() ? QStringLiteral("unknown") : commit;
}

bool ServerConfig::buildModified()
{
    return MEETUP_MODIFIED != 0;
}

QString ServerConfig::buildTime()
{
    return QString::fromLatin1(MEETUP_BUILD_TIME);
}
