#include "Cli.h"
#include <QCommandLineParser>
#include <QUrl>

Cli::Cli(QObject* parent) : QObject(parent) {}

void Cli::parse(const QStringList& args) {
    QCommandLineParser p;
    p.setApplicationDescription("MeetUp — конференции с открытым исходным кодом.");
    const QCommandLineOption oServer({"s", "server"},
        "Адрес сервера: meetup.example.net или https://meetup.example.net.", "адрес");
    const QCommandLineOption oRoom({"r", "room"}, "Код комнаты — войти сразу в неё.", "код");
    const QCommandLineOption oName({"n", "name"}, "Имя для входа гостем.", "имя");
    const QCommandLineOption oKey("key",
        "Ключ шифрования в том виде, в каком он стоит в ссылке после #k=.", "ключ");
    const QCommandLineOption oPhrase("phrase",
        "Ключ шифрования словами — та же фраза, что вводится в настройках.", "фраза");
    const QCommandLineOption oPass("password",
        "Пароль комнаты, если вход в неё закрыт паролем.", "пароль");
    p.addOptions({oServer, oRoom, oName, oKey, oPhrase, oPass});
    p.addHelpOption();
    p.addVersionOption();
    p.addPositionalArgument("ссылка", "Приглашение целиком — вместо остальных ключей.");

    // Именно parse(), а не process(): последний на неизвестном ключе печатает
    // справку в консоль и убивает процесс. У графической программы консоли
    // может не быть вовсе, и человек увидел бы молчаливое ничто вместо окна.
    if (!p.parse(args)) {
        m_error = p.errorText();
        return;
    }
    // Обе эти функции не возвращают управление — они печатают текст и выходят.
    // На Windows без консоли Qt показывает его окном, так что «--help» у
    // графической программы всё-таки работает.
    if (p.isSet("help")) p.showHelp(0);
    if (p.isSet("version")) p.showVersion();

    m_server = p.value(oServer).trimmed();
    m_room = p.value(oRoom).trimmed();
    m_name = p.value(oName).trimmed();
    m_key = p.value(oKey).trimmed();
    m_phrase = p.value(oPhrase);
    m_password = p.value(oPass);
    const QStringList rest = p.positionalArguments();
    if (!rest.isEmpty()) m_link = rest.first().trimmed();

    const bool anything = !m_server.isEmpty() || !m_room.isEmpty() || !m_name.isEmpty()
                          || !m_key.isEmpty() || !m_phrase.isEmpty() || !m_link.isEmpty()
                          || !m_password.isEmpty();
    if (!anything) return;                       // обычный запуск, без аргументов

    if (m_link.isEmpty() && m_room.isEmpty() && m_server.isEmpty()) {
        m_error = "Укажите комнату (--room) или сервер (--server) — "
                  "без них непонятно, что открывать.";
        return;
    }
    m_pending = true;
}

QString Cli::inviteLink(const QString& defaultServer) const {
    if (!m_link.isEmpty()) return m_link;        // прислали готовую — не трогаем
    if (m_room.isEmpty()) return {};

    QString base = m_server.isEmpty() ? defaultServer : m_server;
    base = base.trimmed();
    while (base.endsWith('/')) base.chop(1);
    if (!base.isEmpty()
        && !base.startsWith("http://", Qt::CaseInsensitive)
        && !base.startsWith("https://", Qt::CaseInsensitive))
        base = "https://" + base;

    // Без адреса остаётся голый код: LinkController поймёт его как комнату на
    // текущем сервере. Ключ в этом случае передать некуда — он живёт во
    // фрагменте адреса, а фрагмента у голого кода нет.
    if (base.isEmpty()) return m_room;

    QString url = base + "/conference.html?room="
                  + QString::fromLatin1(QUrl::toPercentEncoding(m_room));
    if (!m_key.isEmpty()) url += "#k=" + m_key;
    return url;
}

QString Cli::takePassword() {
    const QString p = m_password;
    m_password.clear();
    return p;
}

QStringList Cli::restartToServer(const QString& server) {
    QStringList a;
    if (!server.isEmpty()) a << "--server" << server;
    return a;
}

void Cli::consume() {
    if (!m_pending) return;
    m_pending = false;
    emit pendingChanged();
}
