// Стенд отправки Ferry без интерфейса: тот же FerrySender, что у листа
// отправки в чате, но из командной строки.
//
//   ferry-send-tool <релей> <путь>... [--password <фраза>] [--uses <n>]
//                   [--leave-open] [--approve-all] [--seconds <n>] [--revoke]
//
// Печатает ссылку, ход раздачи и стук получателей. С --approve-all пускает
// каждого, кто постучал. Через --seconds уходит (или закрывает доступ).
//
// Сборка: cmake --build out/build/release --target ferry-send-tool
#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

#include "Cli/Password.h"
#include "core/Crypto.h"
#include "core/Protocol.h"
#include "../src/ferry/FerryCargo.h"
#include "../src/ferry/FerrySender.h"

namespace {

void say(const QString &s)
{
    std::printf("%s\n", s.toUtf8().constData());
    std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        say(QStringLiteral("usage: ferry-send-tool <relay> <path>... [--password p] [--uses n] [--leave-open] "
                           "[--approve-all] [--seconds n] [--revoke]"));
        return 2;
    }
    FerrySender::Setup s;
    s.origin = args.at(1);
    QStringList paths;
    QString password;
    int seconds = 60;
    bool approveAll = false;
    bool revoke = false;
    for (int i = 2; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == QLatin1String("--password") && i + 1 < args.size())
            password = args.at(++i);
        else if (a == QLatin1String("--uses") && i + 1 < args.size())
            s.policy.uses = args.at(++i).toLongLong();
        else if (a == QLatin1String("--seconds") && i + 1 < args.size())
            seconds = args.at(++i).toInt();
        else if (a == QLatin1String("--leave-open"))
            s.policy.leaveOpen = true;
        else if (a == QLatin1String("--approve-all")) {
            approveAll = true;
            s.policy.requireApproval = true;
        } else if (a == QLatin1String("--revoke"))
            revoke = true;
        else
            paths.append(a);
    }

    const FerryCargo cargo = FerryCargo::scan(paths);
    if (!cargo.error.isEmpty()) {
        say(QStringLiteral("груз: %1").arg(cargo.error));
        return 2;
    }
    say(QStringLiteral("груз: «%1» %2, %3 байт, файлов %4, пустых папок %5, пропущено %6")
            .arg(QString::fromStdString(cargo.manifest.name), QString::fromStdString(cargo.manifest.kind))
            .arg(cargo.manifest.total).arg(cargo.files).arg(cargo.dirs).arg(cargo.skipped.size()));
    for (const QString &sk : cargo.skipped)
        say(QStringLiteral("  пропущено: %1").arg(sk));
    s.manifest = cargo.manifest;
    s.sources = cargo.sources;
    if (!password.isEmpty()) {
        const std::string norm = ferry::cli::normalizePassword(password.toStdString());
        s.salt.resize(int(ferry::kPasswordSaltSize));
        ferry::randomBytes(reinterpret_cast<uint8_t *>(s.salt.data()), size_t(s.salt.size()));
        if (!ferry::cli::passwordKey(norm, reinterpret_cast<const uint8_t *>(s.salt.constData()),
                                     size_t(s.salt.size()), s.passwordKey)) {
            say(QStringLiteral("argon2 не посчитался"));
            return 2;
        }
        s.password = true;
    }

    auto *tx = new FerrySender(std::move(s), &app);
    QString lastPhase;
    QObject::connect(tx, &FerrySender::ready, &app,
                     [&](const QString &link, const QString &owner, const QString &, const QString &) {
                         say(QStringLiteral("LINK %1").arg(link));
                         say(QStringLiteral("OWNER %1").arg(owner));
                         QTimer::singleShot(seconds * 1000, tx, [tx, revoke] { tx->stop(revoke); });
                     });
    QObject::connect(tx, &FerrySender::progress, &app, [&](const QVariantMap &m) {
        const QString ph = m.value(QStringLiteral("phase")).toString();
        if (ph != lastPhase) {
            say(QStringLiteral("phase: %1").arg(ph));
            lastPhase = ph;
        }
    });
    QObject::connect(tx, &FerrySender::knock, &app, [&](qint64 id, const QString &name) {
        say(QStringLiteral("knock: %1 «%2»").arg(id).arg(name));
        if (approveAll)
            tx->approve(id, true);
    });
    QObject::connect(tx, &FerrySender::relayState, &app, [&](const QVariantMap &v) {
        say(QStringLiteral("state: %1 uses_left=%2 receivers=%3")
                .arg(v.value(QStringLiteral("state")).toString())
                .arg(v.value(QStringLiteral("uses_left")).toString())
                .arg(v.value(QStringLiteral("receivers")).toInt()));
    });
    QObject::connect(tx, &FerrySender::failed, &app, [&](const QString &c, const QString &m) {
        say(QStringLiteral("failed: %1 %2").arg(c, m));
        app.exit(3);
    });
    QObject::connect(tx, &FerrySender::closed, &app, [&] {
        // Даём уйти маяку /leave и close-кадру.
        QTimer::singleShot(1500, &app, [&] {
            say(QStringLiteral("closed: отправлено %1").arg(lastPhase));
            app.exit(0);
        });
    });
    tx->start();
    return app.exec();
}
