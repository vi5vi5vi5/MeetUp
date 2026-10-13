#include "FerryCargo.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <algorithm>

#include "core/VolumeLayout.h"

namespace {

// Потолки из §3 Ferry: опись целиком едет внутри зашифрованного манифеста.
constexpr size_t kMaxEntries = 100000;
constexpr int kMaxDepth = 64;

struct Item
{
    ferry::ManifestEntry entry;
    QString source;   // пусто у пустой папки
};

struct Collector
{
    std::vector<Item> items;
    QStringList skipped;
    QString error;
    quint64 files = 0;
    quint64 dirs = 0;

    bool fail(const QString &why)
    {
        if (error.isEmpty())
            error = why;
        return false;
    }

    // Имя, которое не пройдёт на той стороне (Windows-запреты, «..»,
    // управляющие символы), — не повод отказывать всему тому: в «пропущено».
    bool nameOk(const QString &rel)
    {
        std::string why;
        if (ferry::sanitizeRelPath(rel.toStdString(), &why))
            return true;
        skipped.append(QStringLiteral("%1 — %2").arg(rel, QString::fromStdString(why)));
        return false;
    }

    bool addFile(const QFileInfo &fi, const QString &rel)
    {
        if (!nameOk(rel))
            return true;
        if (items.size() >= kMaxEntries)
            return fail(QStringLiteral("В томе больше %1 файлов — столько за раз не возим.").arg(kMaxEntries));
        // Файл, который не открывается, выясняем СЕЙЧАС, а не через час
        // раздачи: держит другая программа, нет прав, не пускает антивирус.
        if (fi.size() > 0) {
            QFile probe(fi.absoluteFilePath());
            if (!probe.open(QIODevice::ReadOnly)) {
                skipped.append(QStringLiteral("%1 — не читается: %2").arg(rel, probe.errorString()));
                return true;
            }
        }
        Item it;
        it.entry.path = rel.toStdString();
        it.entry.size = quint64(fi.size());
        it.entry.mtime = fi.lastModified().toSecsSinceEpoch();
        it.source = fi.absoluteFilePath();
        items.push_back(std::move(it));
        ++files;
        return true;
    }

    // rel — путь от корня тома (пустой для корня).
    bool walk(const QString &dir, const QString &rel, int depth)
    {
        if (depth > kMaxDepth)
            return fail(QStringLiteral("Дерево глубже %1 уровней — похоже на цикл.").arg(kMaxDepth));
        QFileInfoList list = QDir(dir).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
        // Порядок — побайтовый по UTF-8, как у CLI и веба: при нём любой
        // подкаталог оказывается непрерывным куском тома.
        std::sort(list.begin(), list.end(), [](const QFileInfo &a, const QFileInfo &b) {
            return a.fileName().toUtf8() < b.fileName().toUtf8();
        });
        bool hadChildren = false;
        for (const QFileInfo &fi : list) {
            const QString childRel = rel.isEmpty() ? fi.fileName() : rel + QLatin1Char('/') + fi.fileName();
            // Ярлыки и ссылки не возим: разыменовать — значит утащить полдиска
            // или уйти в цикл, а как есть на той стороне цели может не быть.
            if (fi.isSymLink() || fi.isJunction()) {
                skipped.append(QStringLiteral("%1 — ярлык").arg(childRel));
                continue;
            }
            if (fi.isDir()) {
                hadChildren = true;
                if (!walk(fi.absoluteFilePath(), childRel, depth + 1))
                    return false;
                continue;
            }
            if (!fi.isFile()) {
                skipped.append(QStringLiteral("%1 — не обычный файл").arg(childRel));
                continue;
            }
            hadChildren = true;
            if (!addFile(fi, childRel))
                return false;
        }
        // Пустая папка — отдельной записью, иначе она не доедет вовсе.
        if (!hadChildren && !rel.isEmpty() && nameOk(rel)) {
            Item it;
            it.entry.path = rel.toStdString();
            it.entry.isDir = true;
            items.push_back(std::move(it));
            ++dirs;
        }
        return true;
    }
};

QString looseName()
{
    return QStringLiteral("ferry-") + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace

FerryCargo FerryCargo::scan(const QStringList &paths)
{
    FerryCargo out;
    if (paths.isEmpty()) {
        out.error = QStringLiteral("Ничего не выбрано.");
        return out;
    }

    // Один файл — том-файл, как есть.
    if (paths.size() == 1) {
        const QFileInfo fi(paths.first());
        if (fi.isFile() && !fi.isSymLink()) {
            const std::string name = ferry::safeFileName(fi.fileName().toStdString());
            if (name.empty()) {
                out.error = QStringLiteral("Имя файла не годится для другой стороны.");
                return out;
            }
            QFile probe(fi.absoluteFilePath());
            if (fi.size() > 0 && !probe.open(QIODevice::ReadOnly)) {
                out.error = QStringLiteral("Файл не читается: %1").arg(probe.errorString());
                return out;
            }
            out.manifest.kind = "file";
            out.manifest.name = name;
            out.manifest.total = quint64(fi.size());
            out.files = 1;
            if (fi.size() > 0)
                out.sources.push_back(fi.absoluteFilePath());
            return out;
        }
    }

    Collector c;
    std::string rootName;
    if (paths.size() == 1 && QFileInfo(paths.first()).isDir()) {
        const QFileInfo fi(paths.first());
        rootName = ferry::safeFileName(fi.fileName().toStdString());
        if (!c.walk(fi.absoluteFilePath(), QString(), 0)) {
            out.error = c.error;
            return out;
        }
    } else {
        rootName = looseName().toStdString();
        QHash<QString, bool> taken;
        for (const QString &p : paths) {
            const QFileInfo fi(p);
            const QString name = fi.fileName();
            if (taken.contains(name.toCaseFolded())) {
                c.skipped.append(QStringLiteral("%1 — такое имя в грузе уже есть").arg(name));
                continue;
            }
            taken.insert(name.toCaseFolded(), true);
            if (fi.isSymLink() || fi.isJunction()) {
                c.skipped.append(QStringLiteral("%1 — ярлык").arg(name));
            } else if (fi.isDir()) {
                if (!c.walk(fi.absoluteFilePath(), name, 1)) {
                    out.error = c.error;
                    return out;
                }
            } else if (fi.isFile()) {
                if (!c.addFile(fi, name)) {
                    out.error = c.error;
                    return out;
                }
            } else {
                c.skipped.append(QStringLiteral("%1 — не обычный файл").arg(name));
            }
        }
    }
    if (rootName.empty()) {
        out.error = QStringLiteral("Имя папки не годится для другой стороны.");
        return out;
    }
    if (c.items.empty()) {
        out.error = c.skipped.isEmpty() ? QStringLiteral("Пусто — везти нечего.")
                                        : QStringLiteral("Ни один файл не годится для отправки.");
        out.skipped = c.skipped;
        return out;
    }

    // Порядок описи задаётся ПОЛНЫМИ путями: «a/b» и «a.txt» с разных
    // уровней встретились бы при обходе иначе.
    std::sort(c.items.begin(), c.items.end(), [](const Item &a, const Item &b) {
        return a.entry.path < b.entry.path;
    });

    out.manifest.kind = "tree";
    out.manifest.name = rootName;
    QHash<QString, QString> sourceOf;
    for (Item &it : c.items) {
        out.manifest.total += it.entry.size;
        if (!it.entry.isDir)
            sourceOf.insert(QString::fromStdString(it.entry.path), it.source);
        out.manifest.entries.push_back(std::move(it.entry));
    }
    // Источники — по раскладке тома: в ней нет пустых файлов и папок, и
    // индекс файла раскладки должен указывать на свой путь на диске.
    ferry::VolumeLayout layout;
    layout.build(out.manifest);
    for (const auto &f : layout.files())
        out.sources.push_back(sourceOf.value(QString::fromStdString(f.path)));
    out.skipped = c.skipped;
    out.files = c.files;
    out.dirs = c.dirs;
    return out;
}
