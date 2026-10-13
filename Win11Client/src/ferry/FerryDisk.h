#pragma once

// Том на диске — одно байтовое пространство поверх одного файла или дерева.
//
// Релей видит линейный поток чанков и не знает, один там файл или сорок
// тысяч; приём тоже не должен: он пишет «байты тома по смещению», а на
// границе файлов чанк раскладывается на куски (VolumeLayout::slice из ядра).
// Зеркало Cli/VolumeFile в репозитории Ferry, только на QFile.
//
// Пишем и читаем ПО СМЕЩЕНИЮ, без общего курсора: чанки приходят вразнобой
// (две волны, докачка).

#include <QString>

#include <memory>
#include <vector>

#include "core/Manifest.h"
#include "core/VolumeLayout.h"

class QFile;

class FerryDisk
{
public:
    FerryDisk();
    ~FerryDisk();
    FerryDisk(const FerryDisk &) = delete;
    FerryDisk &operator=(const FerryDisk &) = delete;

    // root — сам файл (том из одного файла) или каталог (дерево); пути
    // раскладки дерева — относительные, от него. Файлы дерева создаются
    // ЛЕНИВО, при первой записи: сорок тысяч пустых файлов заранее — это
    // минуты до первого принятого байта.
    void open(const QString &root, bool tree, const ferry::VolumeLayout *layout);

    // Пустые файлы и пустые каталоги: байт в том они не вносят, а на диске
    // быть обязаны. Для тома-файла — сам файл (нулевой длины тоже).
    bool prepare(const ferry::Manifest &manifest, QString *err);

    bool writeAt(quint64 offset, const uint8_t *data, quint64 len, QString *err);
    // Чтение открывает файл на один вызов: держать их открытыми значило бы
    // не дать человеку ни переименовать, ни удалить принятое, пока идёт
    // раздача другим.
    bool readAt(quint64 offset, uint8_t *data, quint64 len) const;

    bool flush(QString *err);
    void close();

    const QString &root() const { return m_root; }
    void setRoot(const QString &root) { close(); m_root = root; }

private:
    QString pathOf(size_t file) const;
    QFile *writable(size_t file, QString *err);

    struct Slot
    {
        size_t file = size_t(-1);
        std::unique_ptr<QFile> fd;
    };

    QString m_root;
    bool m_tree = false;
    const ferry::VolumeLayout *m_layout = nullptr;   // не владеем
    std::vector<Slot> m_slots;                      // кольцо открытых на запись
    size_t m_next = 0;
    std::vector<bool> m_prepared;                   // файл уже создан и растянут
};
