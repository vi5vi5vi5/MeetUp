#pragma once

// Груз для отправки: из того, что человек выбрал или перетащил, — опись тома
// Ferry. Зеркало Cli/Tree.cpp и Server/web/assets/js/cargo.js из репозитория
// Ferry, потому что получатель у всех один и тот же:
//   * один файл — том вида file;
//   * одна папка — том вида tree с её именем;
//   * несколько файлов (и папок) — tree с именем «ferry-ГГГГ-ММ-ДД»;
//   * пути относительные, POSIX, отсортированы побайтово в UTF-8;
//   * пустая папка — отдельная запись, иначе она не доедет;
//   * ярлыки, ссылки, нечитаемое и имена, которые не пройдут на другой
//     стороне, — в «пропущено» с причиной, до ссылки, а не молча.
//
// Обход папки на сорок тысяч файлов — секунды, поэтому scan зовут не на
// GUI-потоке (см. FerryHub::prepare).

#include <QString>
#include <QStringList>

#include <vector>

#include "core/Manifest.h"

struct FerryCargo
{
    ferry::Manifest manifest;      // без корня: хеши ещё не посчитаны
    std::vector<QString> sources;  // абсолютный путь на каждый файл раскладки тома
    QStringList skipped;           // «путь — почему не поедет»
    quint64 files = 0;
    quint64 dirs = 0;              // пустых папок
    QString error;                 // не пусто — собрать том нельзя

    static FerryCargo scan(const QStringList &paths);
};
