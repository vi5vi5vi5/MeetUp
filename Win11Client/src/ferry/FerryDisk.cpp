#include "FerryDisk.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

// Кольцо на восемь открытых файлов. Обе волны пишут почти подряд, так что
// попадание почти всегда в первый же элемент, а потолок нужен, чтобы том из
// сорока тысяч файлов не упёрся в лимит открытых дескрипторов.
constexpr size_t kSlots = 8;

} // namespace

FerryDisk::FerryDisk() = default;
FerryDisk::~FerryDisk() { close(); }

void FerryDisk::open(const QString &root, bool tree, const ferry::VolumeLayout *layout)
{
    close();
    m_root = root;
    m_tree = tree;
    m_layout = layout;
    m_slots.clear();
    m_slots.resize(kSlots);
    m_next = 0;
    m_prepared.assign(layout ? layout->files().size() : 0, false);
}

QString FerryDisk::pathOf(size_t file) const
{
    if (!m_tree)
        return m_root;
    return m_root + QLatin1Char('/') + QString::fromStdString(m_layout->files()[file].path);
}

bool FerryDisk::prepare(const ferry::Manifest &manifest, QString *err)
{
    if (!m_tree) {
        if (!QDir().mkpath(QFileInfo(m_root).absolutePath())) {
            *err = QStringLiteral("Не создать папку %1").arg(QFileInfo(m_root).absolutePath());
            return false;
        }
        // Файл нулевой длины — законный том: чанков нет, писать нечего, а
        // файл на диске быть обязан.
        if (manifest.total == 0) {
            QFile f(m_root);
            if (!f.open(QIODevice::WriteOnly)) {
                *err = f.errorString();
                return false;
            }
        }
        return true;
    }
    if (!QDir().mkpath(m_root)) {
        *err = QStringLiteral("Не создать папку %1").arg(m_root);
        return false;
    }
    for (const ferry::ManifestEntry &e : manifest.entries) {
        const QString path = m_root + QLatin1Char('/') + QString::fromStdString(e.path);
        if (e.isDir) {
            if (!QDir().mkpath(path)) {
                *err = QStringLiteral("Не создать папку %1").arg(path);
                return false;
            }
        } else if (e.size == 0) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                *err = f.errorString();
                return false;
            }
        }
    }
    return true;
}

QFile *FerryDisk::writable(size_t file, QString *err)
{
    for (Slot &s : m_slots) {
        if (s.file == file && s.fd)
            return s.fd.get();
    }
    Slot &slot = m_slots[m_next];
    m_next = (m_next + 1) % m_slots.size();
    if (slot.fd) {
        slot.fd->flush();
        slot.fd.reset();
    }

    const QString path = pathOf(file);
    if (!m_prepared[file])
        QDir().mkpath(QFileInfo(path).absolutePath());
    auto fd = std::make_unique<QFile>(path);
    // ReadWrite, а не WriteOnly: последний обрезал бы файл, а это может быть
    // недокачка, к которой мы вернулись.
    // Без буфера Qt: чанк могут попросить отдать другому сразу после записи,
    // а чтение идёт через свой дескриптор и буфера записи не видит.
    if (!fd->open(QIODevice::ReadWrite | QIODevice::Unbuffered)) {
        *err = QStringLiteral("%1: %2").arg(path, fd->errorString());
        return nullptr;
    }
    // Размер ставим сразу: дальше куски ложатся по своим местам в любом
    // порядке, а нехватка места всплывает здесь, а не на середине приёма.
    const quint64 want = m_layout->files()[file].size;
    if (!m_prepared[file] && quint64(fd->size()) != want && !fd->resize(qint64(want))) {
        *err = QStringLiteral("%1: %2").arg(path, fd->errorString());
        return nullptr;
    }
    m_prepared[file] = true;
    slot.file = file;
    slot.fd = std::move(fd);
    return slot.fd.get();
}

bool FerryDisk::writeAt(quint64 offset, const uint8_t *data, quint64 len, QString *err)
{
    if (len == 0)
        return true;
    const auto pieces = m_layout->slice(offset, len);
    if (pieces.empty()) {
        *err = QStringLiteral("кусок за пределами тома");
        return false;
    }
    quint64 done = 0;
    for (const auto &p : pieces) {
        QFile *fd = writable(p.file, err);
        if (!fd)
            return false;
        if (!fd->seek(qint64(p.offset))
            || fd->write(reinterpret_cast<const char *>(data + done), qint64(p.length)) != qint64(p.length)) {
            *err = QStringLiteral("%1: %2").arg(fd->fileName(), fd->errorString());
            return false;
        }
        done += p.length;
    }
    return true;
}

bool FerryDisk::readAt(quint64 offset, uint8_t *data, quint64 len) const
{
    if (len == 0)
        return true;
    const auto pieces = m_layout->slice(offset, len);
    if (pieces.empty())
        return false;
    quint64 done = 0;
    for (const auto &p : pieces) {
        QFile f(pathOf(p.file));
        if (!f.open(QIODevice::ReadOnly) || !f.seek(qint64(p.offset))
            || f.read(reinterpret_cast<char *>(data + done), qint64(p.length)) != qint64(p.length))
            return false;
        done += p.length;
    }
    return true;
}

bool FerryDisk::flush(QString *err)
{
    for (Slot &s : m_slots) {
        if (s.fd && !s.fd->flush()) {
            *err = QStringLiteral("%1: %2").arg(s.fd->fileName(), s.fd->errorString());
            return false;
        }
    }
    return true;
}

void FerryDisk::close()
{
    for (Slot &s : m_slots) {
        if (s.fd)
            s.fd->flush();
        s.fd.reset();
        s.file = size_t(-1);
    }
}
