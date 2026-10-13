// Стенд приёма Ferry без интерфейса: тот же FerryReceiver, что у карточки в
// чате, но из командной строки. Нужен, чтобы проверять совместимость с
// настоящим релеем и CLI Ferry, не трогая окно приложения.
//
//   ferry-recv-tool <ссылка> <папка> [--password <фраза>] [--seed <секунд>]
//                   [--stop-after <байт>] [--name <имя>]
//
// --stop-after — оборвать приём, приняв столько байт (проверка докачки:
// второй запуск с той же папкой продолжает с места). Состояние лежит рядом
// с папкой в <папка>/.ferry-state.json.
//
// Сборка: cmake --build out/build/release --target ferry-recv-tool
#include <QCoreApplication>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

#include <cstdio>

#include "Cli/Password.h"
#include "core/Link.h"
#include "core/Protocol.h"
#include "../src/ferry/FerryReceiver.h"
#include "../src/ferry/FerryVolume.h"

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
        say(QStringLiteral("usage: ferry-recv-tool <link> <dir> [--password p] [--seed s] [--stop-after bytes]"));
        return 2;
    }
    const QString linkText = args.at(1);
    const QString dir = QDir(args.at(2)).absolutePath();
    QString password;
    int seedSeconds = 0;
    double stopAfter = -1;
    QString name = QStringLiteral("стенд");
    for (int i = 3; i + 1 < args.size(); i += 2) {
        if (args.at(i) == QLatin1String("--password"))
            password = args.at(i + 1);
        else if (args.at(i) == QLatin1String("--seed"))
            seedSeconds = args.at(i + 1).toInt();
        else if (args.at(i) == QLatin1String("--stop-after"))
            stopAfter = args.at(i + 1).toDouble();
        else if (args.at(i) == QLatin1String("--name"))
            name = args.at(i + 1);
    }

    ferry::TransferLink link;
    std::string err;
    if (!ferry::TransferLink::parse(linkText.toStdString(), link, &err)) {
        say(QStringLiteral("ссылка: %1").arg(QString::fromStdString(err)));
        return 2;
    }
    const QString origin = QString::fromStdString(link.origin());
    const QString id = QString::fromStdString(link.id);
    QDir().mkpath(dir);

    QNetworkAccessManager nam;
    QNetworkRequest req = FerryApi::request(origin, QStringLiteral("/api/transfers/") + id);
    req.setRawHeader(ferry::kFeaturesHeader, ferry::kFeatureStreamHashes);
    QNetworkReply *reply = nam.get(req);
    FerryReceiver *rx = nullptr;
    int exitCode = 1;
    QString lastPhase;

    QObject::connect(reply, &QNetworkReply::finished, &app, [&] {
        const QByteArray body = reply->readAll();
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            say(QStringLiteral("meta: %1").arg(FerryApi::errorCode(reply, body)));
            app.exit(1);
            return;
        }
        FerryMeta meta;
        if (!FerryMeta::parse(body, meta)) {
            say(QStringLiteral("meta: не разобрался"));
            app.exit(1);
            return;
        }
        say(QStringLiteral("meta: total=%1 chunk=%2 chunks=%3 mode=%4 stream=%5 hashed=%6 state=%7")
                .arg(meta.total).arg(meta.chunkSize).arg(meta.chunks)
                .arg(meta.password ? "password" : "key").arg(meta.stream).arg(meta.hashed).arg(meta.state));
        ferry::Key32 key = link.key;
        if (meta.password) {
            const std::string norm = ferry::cli::normalizePassword(password.toStdString());
            if (!ferry::cli::passwordKey(norm, reinterpret_cast<const uint8_t *>(meta.salt.constData()),
                                         size_t(meta.salt.size()), key)) {
                say(QStringLiteral("argon2 не посчитался"));
                app.exit(1);
                return;
            }
        } else if (!link.hasKey) {
            say(QStringLiteral("нет ключа"));
            app.exit(1);
            return;
        }
        FerryVolume vol;
        QString code, text;
        if (!FerryVolume::open(meta, key, vol, &code, &text)) {
            say(QStringLiteral("volume: %1 %2").arg(code, text));
            app.exit(1);
            return;
        }
        say(QStringLiteral("том: «%1» %2, файлов %3, пустых папок %4")
                .arg(QString::fromStdString(vol.manifest.name), QString::fromStdString(vol.manifest.kind))
                .arg(vol.files).arg(vol.dirs));

        FerryReceiver::Setup s;
        s.origin = origin;
        s.id = id;
        s.meta = meta;
        s.volume = vol;
        s.finalDir = dir;
        s.partPath = dir + QLatin1Char('/') + QString::fromStdString(ferry::safeFileName(vol.manifest.name))
                     + QStringLiteral(".ferry-part");
        s.stateFile = dir + QStringLiteral("/.ferry-state.json");
        s.name = name;
        s.seeding = seedSeconds > 0;
        rx = new FerryReceiver(std::move(s), &app);
        QObject::connect(rx, &FerryReceiver::progress, &app, [&, rx](const QVariantMap &m) {
            const QString ph = m.value(QStringLiteral("phase")).toString();
            const double bytes = m.value(QStringLiteral("bytes")).toDouble();
            if (ph != lastPhase) {
                say(QStringLiteral("phase: %1 (resumed=%2, bytes=%3)")
                        .arg(ph).arg(m.value(QStringLiteral("resumed")).toBool()).arg(bytes, 0, 'f', 0));
                lastPhase = ph;
            }
            if (stopAfter >= 0 && bytes >= stopAfter && ph == QLatin1String("receiving")) {
                say(QStringLiteral("обрываем на %1 байтах").arg(bytes, 0, 'f', 0));
                stopAfter = -1;
                QMetaObject::invokeMethod(rx, &FerryReceiver::stop, Qt::QueuedConnection);
            }
        });
        QObject::connect(rx, &FerryReceiver::failed, &app, [&](const QString &c, const QString &m, bool retry) {
            say(QStringLiteral("failed: %1 %2 retry=%3").arg(c, m).arg(retry));
            app.exit(3);
        });
        QObject::connect(rx, &FerryReceiver::stopped, &app, [&] {
            say(QStringLiteral("stopped"));
            app.exit(exitCode);
        });
        QObject::connect(rx, &FerryReceiver::done, &app, [&, rx](const QString &path) {
            say(QStringLiteral("done: %1").arg(path));
            exitCode = 0;
            QTimer::singleShot(seedSeconds * 1000, rx, &FerryReceiver::stop);
        });
        rx->start();
    });
    return app.exec();
}
