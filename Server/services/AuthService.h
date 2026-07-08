#pragma once

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
class AuthService
{
public:
    AuthService(std::shared_ptr<IUsers> users, std::shared_ptr<ISessions> sessions);

    AuthResult registerUser(const QString &rawLogin, const QString &password,
                            const QString &rawDisplayName);
    AuthResult login(const QString &rawLogin, const QString &password);
    void logout(const QString &token);

    // Пользователь по токену сессии; сессия скользяще продлевается.
    std::optional<User> userByToken(const QString &token);

    AuthResult changeDisplayName(const QString &token, const QString &rawDisplayName);

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

    std::shared_ptr<IUsers> m_users;
    std::shared_ptr<ISessions> m_sessions;
};
