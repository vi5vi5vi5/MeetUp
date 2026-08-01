#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

// Аргументы командной строки: чем открыться при запуске. В QML — Cli.
//
// Две задачи разом. Первая — ярлык, который сразу заводит в нужную комнату на
// нужном сервере (`MeetUp.exe --server meetup.linkpc.net --room standup`).
// Вторая — возврат после обновления: перезапускаясь, клиент передаёт себе
// самому, где он был (см. UpdateChecker::installAndRestart).
//
// Порядок разбора ровно такой:
//   * задана комната — идём в неё, даже если на этом сервере есть сохранённая
//     сессия аккаунта: явный аргумент главнее того, что помнит программа;
//   * комнаты нет, но задан сервер — переключаемся на него и входим
//     сохранённой сессией, как при обычном запуске;
//   * нет ни того, ни другого, но что-то передано — это ошибка, и молчать о
//     ней нельзя: человек написал ярлык и вправе узнать, почему тот не сработал.
// Пустая командная строка — обычный запуск, никаких сообщений.
//
// Про ключ E2E в аргументах: он виден в списке процессов, и это осознанная
// плата за ярлык, который открывает зашифрованную комнату без ввода фразы.
// Ключ живёт только до конца разговора (см. E2eController::onLeft) и на диск
// по-прежнему не попадает.
class Cli : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString server READ server CONSTANT)
    Q_PROPERTY(QString room READ room CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString phrase READ phrase CONSTANT)
    // Ссылка-приглашение целиком, если её передали вместо разрозненных полей.
    Q_PROPERTY(QString link READ link CONSTANT)
    Q_PROPERTY(QString errorText READ errorText CONSTANT)
    // Есть что применить (и это ещё не применено). Гасится consume().
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
public:
    explicit Cli(QObject* parent = nullptr);

    // args — QCoreApplication::arguments(). Разбирает молча: неизвестные ключи
    // не роняют приложение, потому что уронить графическую программу сообщением
    // в несуществующую консоль — худший из возможных ответов.
    void parse(const QStringList& args);

    QString server() const { return m_server; }
    QString room() const { return m_room; }
    QString name() const { return m_name; }
    QString phrase() const { return m_phrase; }
    QString link() const { return m_link; }
    QString errorText() const { return m_error; }
    bool pending() const { return m_pending; }

    // Собрать то, что понимает LinkController::open(): либо переданную ссылку
    // как есть, либо адрес комнаты, склеенный из аргументов. defaultServer —
    // текущий адрес (его знает Sys), он подставляется, когда --server не задан.
    Q_INVOKABLE QString inviteLink(const QString& defaultServer) const;

    // Пароль комнаты — одноразово: он относится к той комнате, ради которой
    // запускались. Прочитали и забыли, иначе следующая закрытая комната за тот
    // же запуск получила бы его автоматически и молча.
    Q_INVOKABLE QString takePassword();

    // Перезапуск после обновления с главной: вернуться на тот же сервер к тому
    // же аккаунту. Возврат в комнату собирает SignalingClient::restartArgs —
    // там лежит и пароль, которому в QML делать нечего.
    Q_INVOKABLE static QStringList restartToServer(const QString& server);

    // Применили — больше не применять. Проверка сессии на старте отвечает не
    // один раз (её зовёт ещё и смена сервера по ссылке), и без этого флага
    // клиент заходил бы в комнату повторно.
    Q_INVOKABLE void consume();

signals:
    void pendingChanged();

private:
    QString m_server, m_room, m_name, m_key, m_phrase, m_link, m_password, m_error;
    bool m_pending = false;
};
