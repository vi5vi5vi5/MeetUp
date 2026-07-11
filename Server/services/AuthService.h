#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <QString>

#include "interface/Abstract/ISessions.h"
#include "interface/Abstract/IUsers.h"

// Результат операции аутентификации: либо ok с данными, либо машинный код
// ошибки — сетевой слой превращает его в HTTP-статус, клиент — в текст.
struct AuthResult
{
    bool ok = false;
    QString error;      // "invalid_login", "weak_password", "login_taken",
                        // "invalid_display_name", "wrong_credentials", "no_session"
    User user;
    Session session;    // заполнена после register/login

    static AuthResult fail(const char *code)
    {
        AuthResult r;
        r.error = QLatin1String(code);
        return r;
    }
};

// Регистрация, вход и сессии. Пароли аккаунтов хранятся только как
// PBKDF2-HMAC-SHA512 (соль на пользователя, сравнение за константное время);
// сам пароль восстановить нельзя. Про HTTP этот класс не знает.
//
// PBKDF2 намеренно дорогой, поэтому register/login асинхронные: хеш считается
// в пуле потоков, event loop (и видео-релей) не блокируется; done зовётся
// в главном потоке. Репозитории трогаем только из главного потока.
class AuthService
{
public:
    using AuthCallback = std::function<void(const AuthResult &)>;

    AuthService(std::shared_ptr<IUsers> users, std::shared_ptr<ISessions> sessions);

    void registerUserAsync(const QString &rawLogin, const QString &password,
                           const QString &rawDisplayName, AuthCallback done);
    void loginAsync(const QString &rawLogin, const QString &password, AuthCallback done);
    void logout(const QString &token);

    // Пользователь по токену сессии; сессия скользяще продлевается.
    std::optional<User> userByToken(const QString &token);

    AuthResult changeDisplayName(const QString &token, const QString &rawDisplayName);

    // Версия аватарки: сам файл кладёт/удаляет HTTP-слой, сервис обновляет
    // только счётчик в профиле (0 — аватарки нет, растёт с каждой загрузкой).
    AuthResult setAvatarVer(const QString &token, int ver);

    // Периодическая чистка протухших сессий (зовётся по таймеру из main).
    int purgeExpiredSessions();

    static QString normalizeLogin(const QString &raw);
    static bool validLogin(const QString &login);
    static bool validDisplayName(const QString &name);

    // 30 дней жизни сессии; продлевается, когда осталось меньше половины.
    static constexpr qint64 kSessionTtlMs = 30ll * 24 * 3600 * 1000;

private:
    QByteArray hashPassword(const QString &password, const QByteArray &salt, int iters) const;
    Session createSession(int userId);

    // hashFn — в пуле потоков, then(hash) — обратно в главном потоке.
    void hashInPool(std::function<QByteArray()> hashFn,
                    std::function<void(const QByteArray &)> then);

    // Тихо переварить пароль с актуальной стоимостью (после смены kPassIters).
    void rehash(int userId, const QString &password);

    std::shared_ptr<IUsers> m_users;
    std::shared_ptr<ISessions> m_sessions;
};
