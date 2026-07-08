#include "services/AuthService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace {

// OWASP-ориентир для PBKDF2-HMAC-SHA512 — 210 тысяч итераций (~0.1 с на
// вход). Число хранится в User::passIters: поднять стоимость можно в любой
// момент, старые хеши продолжат проверяться со своим значением.
constexpr int kPassIters = 210000;
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

AuthResult AuthService::registerUser(const QString &rawLogin, const QString &password,
                                     const QString &rawDisplayName)
{
    const QString login = normalizeLogin(rawLogin);
    if (!validLogin(login))
        return AuthResult::fail("invalid_login");

    const QString displayName = rawDisplayName.trimmed();
    if (!validDisplayName(displayName))
        return AuthResult::fail("invalid_display_name");

    if (password.size() < kMinPasswordLen || password.size() > kMaxPasswordLen)
        return AuthResult::fail("weak_password");

    if (m_users->findByLogin(login).has_value())
        return AuthResult::fail("login_taken");

    User u;
    u.login = login;
    u.displayName = displayName;
    u.passIters = kPassIters;
    u.passSalt = randomBytes(kSaltLen);
    u.passHash = hashPassword(password, u.passSalt, u.passIters);
    u.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m_users->save(u);

    AuthResult r;
    r.ok = true;
    r.user = u;
    r.session = createSession(u.id);
    return r;
}

AuthResult AuthService::login(const QString &rawLogin, const QString &password)
{
    const QString login = normalizeLogin(rawLogin);
    const std::optional<User> user = m_users->findByLogin(login);

    if (!user.has_value()) {
        // Считаем хеш и для несуществующего логина: иначе по мгновенному
        // ответу можно перечислять занятые логины (timing-оракул).
        static const QByteArray dummySalt(kSaltLen, '\0');
        hashPassword(password, dummySalt, kPassIters);
        return AuthResult::fail("wrong_credentials");
    }

    const QByteArray hash = hashPassword(password, user->passSalt, user->passIters);
    if (!constantTimeEquals(hash, user->passHash))
        return AuthResult::fail("wrong_credentials");

    AuthResult r;
    r.ok = true;
    r.user = *user;
    r.session = createSession(user->id);
    return r;
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

int AuthService::purgeExpiredSessions()
{
    return m_sessions->removeExpired(QDateTime::currentMSecsSinceEpoch());
}
