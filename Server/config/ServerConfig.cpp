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

[auth]
# Пускать ли новых людей регистрироваться: open | closed. Закрыли — кнопки
# регистрации не будет и в клиентах: они спрашивают у сервера, что разрешено.
#
# Значением считается ВСЁ после знака равенства, включая решётки и пробелы в
# середине: дописывать комментарий в ту же строку нельзя.
#registration = open

# Пускать ли в комнаты без аккаунта. false вместе с registration = closed даёт
# полностью частный сервер: войти может только тот, кого завели руками.
#allow_anonymous_join = true

# Сколько дней живёт сессия входа (продлевается на половине срока).
#session_ttl_days = 30

# Нижняя граница длины пароля аккаунта.
#min_password_len = 8

# Стоимость PBKDF2. Поднимать безопасно: старые хеши проверяются со своей
# стоимостью и тихо перевариваются при следующем входе. Опускать — ослабление,
# а не ускорение; имеет смысл только на совсем слабом железе.
#pbkdf2_iters = 64000

[rooms]
# Кто может завести разовую комнату:
#   open    — кто угодно (как было всегда)
#   account — только вошедшие
#   off     — никто; работают только личные комнаты
#
# Это главный вентиль против замусоривания. Ручка создания комнат не требует
# ничего: на открытом сервере с неё снимается несколько тысяч комнат в минуту.
#anonymous_create = open

# Потолок живых разовых комнат; 0 — без потолка. Личных комнат не касается: их
# число ограничено числом аккаунтов, и запирать владельца снаружи из-за чужого
# мусора неправильно.
#max_total = 0

# Сколько секунд пустая комната ждёт сборщика мусора. Пауза нужна, чтобы обрыв
# связи последнего участника не убивал комнату вместе с историей чата.
#idle_ttl_s = 600

# Сколько личных комнат человек может держать за собой. Каждая — со своим
# кодом-ссылкой, паролем и приглашениями.
#max_personal_per_user = 1

# Нижняя граница длины кода личной комнаты. На публичном сервере короткие коды
# разбирают первыми.
#code_min_len = 3

# Ссылок-приглашений на одну комнату.
#max_aliases_per_room = 5

[media]
# Сколько демонстраций экрана может идти в комнате одновременно.
#
# Поднимать дороже, чем кажется: сервер рассылает все потоки всем участникам —
# он не знает, кто на что смотрит, — поэтому исходящий трафик растёт кратно.
# Клиент разбирает только ту демонстрацию, которую видит, но получает все.
# Ориентир: три демонстрации в режиме «Источник» на 4K — это около 18 Мбит/с
# входящих каждому участнику.
#max_screen_shares = 1

[chat]
# Сообщений в истории комнаты — её получает каждый вошедший.
#history_size = 500

# Картинок в истории; у более старых данные освобождаются, текст остаётся.
# Это потолок памяти, а не удобство: комната, набитая картинками до предела,
# держит около 20 МБ — и ещё десять минут после ухода последнего участника.
#history_images = 24

# Потолок картинки в чате, КБ.
#image_max_kb = 440

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

// Целое с границами. Выход за границы — это не «поправим молча», а повод
// сказать вслух: человек, написавший 0 там, где 0 ломает сервер, должен об
// этом узнать, а не гадать, почему ничего не изменилось.
int parseInt(const QString &raw, int fallback, int min, int max,
             const char *where, QStringList *warnings)
{
    bool ok = false;
    const int v = raw.trimmed().toInt(&ok);
    if (!ok) {
        warnings->append(QStringLiteral("%1: «%2» — не число, оставляю %3")
                             .arg(QLatin1String(where), raw, QString::number(fallback)));
        return fallback;
    }
    if (v < min || v > max) {
        warnings->append(QStringLiteral("%1: %2 вне разумных границ (%3…%4), оставляю %5")
                             .arg(QLatin1String(where), QString::number(v),
                                  QString::number(min), QString::number(max),
                                  QString::number(fallback)));
        return fallback;
    }
    return v;
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

    if (const auto v = ini.take("auth", "registration")) {
        const QString mode = v->trimmed().toLower();
        if (mode == QLatin1String("open"))
            cfg.registrationOpen = true;
        else if (mode == QLatin1String("closed"))
            cfg.registrationOpen = false;
        else
            cfg.warnings.append(QStringLiteral("auth.registration: «%1» — не open и не "
                                               "closed, оставляю open").arg(*v));
    }
    if (const auto v = ini.take("auth", "allow_anonymous_join"))
        cfg.allowAnonymousJoin = parseBool(*v, cfg.allowAnonymousJoin,
                                           "auth.allow_anonymous_join", &cfg.warnings);
    if (const auto v = ini.take("auth", "session_ttl_days"))
        cfg.sessionTtlDays = parseInt(*v, cfg.sessionTtlDays, 1, 3650,
                                      "auth.session_ttl_days", &cfg.warnings);
    if (const auto v = ini.take("auth", "min_password_len"))
        cfg.minPasswordLen = parseInt(*v, cfg.minPasswordLen, 1, 128,
                                      "auth.min_password_len", &cfg.warnings);
    if (const auto v = ini.take("auth", "pbkdf2_iters"))
        cfg.pbkdf2Iters = parseInt(*v, cfg.pbkdf2Iters, 1000, 10000000,
                                   "auth.pbkdf2_iters", &cfg.warnings);

    if (const auto v = ini.take("rooms", "anonymous_create")) {
        const QString mode = v->trimmed().toLower();
        if (mode == QLatin1String("open"))
            cfg.anonymousCreate = RoomCreate::Open;
        else if (mode == QLatin1String("account"))
            cfg.anonymousCreate = RoomCreate::Account;
        else if (mode == QLatin1String("off"))
            cfg.anonymousCreate = RoomCreate::Off;
        else
            cfg.warnings.append(QStringLiteral("rooms.anonymous_create: «%1» — не "
                                               "open/account/off, оставляю open").arg(*v));
    }
    if (const auto v = ini.take("rooms", "max_total"))
        cfg.maxTotalRooms = parseInt(*v, cfg.maxTotalRooms, 0, 10000000,
                                     "rooms.max_total", &cfg.warnings);
    if (const auto v = ini.take("rooms", "idle_ttl_s"))
        cfg.roomIdleTtlS = parseInt(*v, cfg.roomIdleTtlS, 10, 86400,
                                    "rooms.idle_ttl_s", &cfg.warnings);
    if (const auto v = ini.take("rooms", "max_personal_per_user"))
        cfg.maxPersonalPerUser = parseInt(*v, cfg.maxPersonalPerUser, 1, 100,
                                          "rooms.max_personal_per_user", &cfg.warnings);
    if (const auto v = ini.take("rooms", "code_min_len"))
        cfg.codeMinLen = parseInt(*v, cfg.codeMinLen, 1, 32,
                                  "rooms.code_min_len", &cfg.warnings);
    if (const auto v = ini.take("rooms", "max_aliases_per_room"))
        cfg.maxAliasesPerRoom = parseInt(*v, cfg.maxAliasesPerRoom, 0, 1000,
                                         "rooms.max_aliases_per_room", &cfg.warnings);

    if (const auto v = ini.take("media", "max_screen_shares"))
        cfg.maxScreenShares = parseInt(*v, cfg.maxScreenShares, 1, 16,
                                       "media.max_screen_shares", &cfg.warnings);

    if (const auto v = ini.take("chat", "history_size"))
        cfg.chatHistorySize = parseInt(*v, cfg.chatHistorySize, 0, 100000,
                                       "chat.history_size", &cfg.warnings);
    if (const auto v = ini.take("chat", "history_images"))
        cfg.chatHistoryImages = parseInt(*v, cfg.chatHistoryImages, 0, 10000,
                                         "chat.history_images", &cfg.warnings);
    if (const auto v = ini.take("chat", "image_max_kb"))
        cfg.chatImageMaxKb = parseInt(*v, cfg.chatImageMaxKb, 1, 100000,
                                      "chat.image_max_kb", &cfg.warnings);

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

QString ServerConfig::anonymousCreateName() const
{
    switch (anonymousCreate) {
    case RoomCreate::Account: return QStringLiteral("account");
    case RoomCreate::Off:     return QStringLiteral("off");
    case RoomCreate::Open:    break;
    }
    return QStringLiteral("open");
}

int ServerConfig::chatImageMaxB64() const
{
    // КБ -> байты -> символы base64 (4 символа на 3 байта). Округляем вверх:
    // потолок должен быть не меньше обещанного человеку, а не на байт меньше.
    return int((qint64(chatImageMaxKb) * 1024 + 2) / 3 * 4);
}
