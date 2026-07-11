#include "services/AuthService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QMetaObject>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QThreadPool>

namespace {

// Стоимость PBKDF2-HMAC-SHA512 подобрана под ~0.1–0.3 с на обычном железе:
// вход не раздражает, а перебор утёкших хешей всё ещё дорог. Число хранится
// в User::passIters — его можно поднять в любой момент: старые хеши
// проверяются со своей стоимостью и тихо перевариваются при входе (rehash).
constexpr int kPassIters = 64000;
constexpr int kSaltLen = 16;
constexpr int kHashLen = 64;

constexpr int kMinPasswordLen = 8;
constexpr int kMaxPasswordLen = 128;

QByteArray randomBytes(int n)
{
    // QRandomGenerator::system() — криптостойкий источник ОС.
    QByteArray out(n, Qt::Uninitialized);
    for (int i = 0; i < n; i += 4) {
        const quint32 r = QRandomGenerator::system()->generate();
        for (int j = 0; j < 4 && i + j < n; ++j)
            out[i + j] = char((r >> (8 * j)) & 0xFF);
    }
    return out;
}

// Сравнение за константное время: обычный operator== выходит на первом
// расхождении и по времени ответа выдаёт длину совпавшего префикса.
bool constantTimeEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    return diff == 0;
}

} // namespace

AuthService::AuthService(std::shared_ptr<IUsers> users, std::shared_ptr<ISessions> sessions)
    : m_users(std::move(users)), m_sessions(std::move(sessions))
{
}

QString AuthService::normalizeLogin(const QString &raw)
{
    return raw.trimmed().toLower();
}

bool AuthService::validLogin(const QString &login)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9_.-]{3,32}$"));
    return re.match(login).hasMatch();
}

bool AuthService::validDisplayName(const QString &name)
{
    if (name.isEmpty() || name.size() > 40)
        return false;
    for (const QChar &c : name)
        if (!c.isPrint())
            return false;
    return true;
}

QByteArray AuthService::hashPassword(const QString &password, const QByteArray &salt, int iters) const
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha512,
                                              password.toUtf8(), salt, iters, kHashLen);
}

Session AuthService::createSession(int userId)
{
    Session s;
    s.token = QString::fromLatin1(randomBytes(32).toBase64(QByteArray::Base64UrlEncoding
                                                           | QByteArray::OmitTrailingEquals));
    s.userId = userId;
    s.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    s.expiresAtMs = s.createdAtMs + kSessionTtlMs;
    m_sessions->save(s);
    return s;
}

void AuthService::hashInPool(std::function<QByteArray()> hashFn,
                             std::function<void(const QByteArray &)> then)
{
    // AuthService живёт до конца программы (main держит shared_ptr), поэтому
    // захват this в продолжениях безопасен.
    QThreadPool::globalInstance()->start([hashFn, then] {
        const QByteArray hash = hashFn();
        // Продолжение — в главном потоке: репозитории не потокобезопасны.
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  [then, hash] { then(hash); },
                                  Qt::QueuedConnection);
    });
}

void AuthService::registerUserAsync(const QString &rawLogin, const QString &password,
                                    const QString &rawDisplayName, AuthCallback done)
{
    const QString login = normalizeLogin(rawLogin);
    if (!validLogin(login)) {
        done(AuthResult::fail("invalid_login"));
        return;
    }

    const QString displayName = rawDisplayName.trimmed();
    if (!validDisplayName(displayName)) {
        done(AuthResult::fail("invalid_display_name"));
        return;
    }

    if (password.size() < kMinPasswordLen || password.size() > kMaxPasswordLen) {
        done(AuthResult::fail("weak_password"));
        return;
    }

    if (m_users->findByLogin(login).has_value()) {
        done(AuthResult::fail("login_taken"));
        return;
    }

    const QByteArray salt = randomBytes(kSaltLen);
    hashInPool(
        [this, password, salt] { return hashPassword(password, salt, kPassIters); },
        [this, login, displayName, salt, done](const QByteArray &hash) {
            // Пока хеш считался, логин могли занять параллельным запросом.
            if (m_users->findByLogin(login).has_value()) {
                done(AuthResult::fail("login_taken"));
                return;
            }

            User u;
            u.login = login;
            u.displayName = displayName;
            u.passIters = kPassIters;
            u.passSalt = salt;
            u.passHash = hash;
            u.createdAtMs = QDateTime::currentMSecsSinceEpoch();
            m_users->save(u);

            AuthResult r;
            r.ok = true;
            r.user = u;
            r.session = createSession(u.id);
            done(r);
        });
}

void AuthService::loginAsync(const QString &rawLogin, const QString &password, AuthCallback done)
{
    const QString login = normalizeLogin(rawLogin);
    const std::optional<User> found = m_users->findByLogin(login);

    if (!found.has_value()) {
        // Хеш считаем и для неизвестного логина: иначе по мгновенному отказу
        // можно перечислять занятые логины (timing-оракул).
        static const QByteArray dummySalt(kSaltLen, '\0');
        hashInPool([this, password] { return hashPassword(password, dummySalt, kPassIters); },
                   [done](const QByteArray &) { done(AuthResult::fail("wrong_credentials")); });
        return;
    }

    const User u = *found;
    hashInPool(
        [this, password, u] { return hashPassword(password, u.passSalt, u.passIters); },
        [this, u, password, done](const QByteArray &hash) {
            if (!constantTimeEquals(hash, u.passHash)) {
                done(AuthResult::fail("wrong_credentials"));
                return;
            }

            // Стоимость хеша устарела (kPassIters подняли) — переварим пароль
            // с актуальной, пока он в руках. Пользователя это не задерживает.
            if (u.passIters != kPassIters)
                rehash(u.id, password);

            AuthResult r;
            r.ok = true;
            r.user = m_users->findById(u.id).value_or(u);
            r.session = createSession(u.id);
            done(r);
        });
}

void AuthService::rehash(int userId, const QString &password)
{
    const QByteArray salt = randomBytes(kSaltLen);
    hashInPool(
        [this, password, salt] { return hashPassword(password, salt, kPassIters); },
        [this, userId, salt](const QByteArray &hash) {
            std::optional<User> u = m_users->findById(userId);
            if (!u.has_value())
                return;   // пользователя успели удалить
            u->passSalt = salt;
            u->passHash = hash;
            u->passIters = kPassIters;
            m_users->save(*u);
        });
}

void AuthService::logout(const QString &token)
{
    if (!token.isEmpty())
        m_sessions->removeByToken(token);
}

std::optional<User> AuthService::userByToken(const QString &token)
{
    if (token.isEmpty())
        return std::nullopt;

    const std::optional<Session> session = m_sessions->findByToken(token);
    if (!session.has_value())
        return std::nullopt;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (session->isExpired(now)) {
        m_sessions->removeByToken(token);
        return std::nullopt;
    }

    const std::optional<User> user = m_users->findById(session->userId);
    if (!user.has_value()) {
        m_sessions->removeByToken(token);   // пользователь удалён — сессия мертва
        return std::nullopt;
    }

    // Скользящее продление: активный пользователь не разлогинится никогда,
    // а покинувший проект — через месяц.
    if (session->expiresAtMs - now < kSessionTtlMs / 2) {
        Session renewed = *session;
        renewed.expiresAtMs = now + kSessionTtlMs;
        m_sessions->save(renewed);
    }
    return user;
}

AuthResult AuthService::changeDisplayName(const QString &token, const QString &rawDisplayName)
{
    std::optional<User> user = userByToken(token);
    if (!user.has_value())
        return AuthResult::fail("no_session");

    const QString displayName = rawDisplayName.trimmed();
    if (!validDisplayName(displayName))
        return AuthResult::fail("invalid_display_name");

    user->displayName = displayName;
    m_users->save(*user);

    AuthResult r;
    r.ok = true;
    r.user = *user;
    return r;
}

AuthResult AuthService::setAvatarVer(const QString &token, int ver)
{
    std::optional<User> user = userByToken(token);
    if (!user.has_value())
        return AuthResult::fail("no_session");

    user->avatarVer = qMax(0, ver);
    m_users->save(*user);

    AuthResult r;
    r.ok = true;
    r.user = *user;
    return r;
}

int AuthService::purgeExpiredSessions()
{
    return m_sessions->removeExpired(QDateTime::currentMSecsSinceEpoch());
}
