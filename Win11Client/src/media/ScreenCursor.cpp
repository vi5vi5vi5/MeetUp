#include "ScreenCursor.h"
#include <QHash>
#include <QMutex>
#include <QString>
#include <QList>
#include <QScreen>
#include <QGuiApplication>
#include <qpa/qplatformnativeinterface.h>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#ifdef Q_OS_WIN

namespace {

struct Cached {
    QImage image;
    QPoint hotspot;
};

// Кэш форм курсора. Обращаются к нему с потока кодирования — замок дешевле,
// чем рассуждать о том, кто ещё туда однажды заглянет.
QMutex g_lock;
QHash<quintptr, Cached> g_cache;

// Прочитать GDI-битмап как 32-битную картинку. Именно GetDIBits, а не
// DrawIconEx: DrawIconEx для цветного курсора делает AlphaBlend на приёмник и
// альфу при этом теряет — на выходе получалась полностью прозрачная картинка
// (проверено пробой). GetDIBits отдаёт байты как есть, вместе с альфой.
QImage readBitmap(HDC dc, HBITMAP bmp, int w, int h) {
    if (!bmp || w <= 0 || h <= 0) return {};
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;                  // строки сверху вниз
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    QImage img(w, h, QImage::Format_ARGB32);
    if (img.isNull()) return {};
    if (!GetDIBits(dc, bmp, 0, UINT(h), img.bits(), &bi, DIB_RGB_COLORS)) return {};
    return img;
}

bool hasAlpha(const QImage& img) {
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(row[x])) return true;
    }
    return false;
}

Cached renderCursor(HCURSOR cur) {
    Cached out;
    ICONINFO ii{};
    if (!GetIconInfo(cur, &ii)) return out;
    out.hotspot = QPoint(int(ii.xHotspot), int(ii.yHotspot));

    HDC dc = GetDC(nullptr);
    BITMAP bm{};

    if (ii.hbmColor && GetObject(ii.hbmColor, sizeof(bm), &bm)) {
        // Обычный цветной курсор.
        const int w = bm.bmWidth, h = bm.bmHeight;
        QImage img = readBitmap(dc, ii.hbmColor, w, h);
        if (!img.isNull() && !hasAlpha(img)) {
            // Курсор без своей альфы: прозрачность задаёт «И»-маска
            // (единица = пиксель не рисуем).
            const QImage mask = readBitmap(dc, ii.hbmMask, w, h);
            for (int y = 0; y < h; ++y) {
                QRgb* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
                const QRgb* m = mask.isNull() ? nullptr
                    : reinterpret_cast<const QRgb*>(mask.constScanLine(y));
                for (int x = 0; x < w; ++x)
                    dst[x] = qRgba(qRed(dst[x]), qGreen(dst[x]), qBlue(dst[x]),
                                   (m && qRed(m[x]) > 127) ? 0 : 255);
            }
        }
        out.image = img;
    } else if (ii.hbmMask && GetObject(ii.hbmMask, sizeof(bm), &bm)) {
        // Чёрно-белый курсор: маска вдвое выше картинки — сверху «И»-маска,
        // снизу «ИСКЛЮЧАЮЩЕЕ ИЛИ». Классическая стрелка устроена именно так.
        const int w = bm.bmWidth, h = bm.bmHeight / 2;
        const QImage both = readBitmap(dc, ii.hbmMask, w, bm.bmHeight);
        if (!both.isNull() && h > 0) {
            QImage img(w, h, QImage::Format_ARGB32);
            for (int y = 0; y < h; ++y) {
                const QRgb* andRow = reinterpret_cast<const QRgb*>(both.constScanLine(y));
                const QRgb* xorRow = reinterpret_cast<const QRgb*>(both.constScanLine(y + h));
                QRgb* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const bool keepOut = qRed(andRow[x]) > 127;   // 1 = прозрачно
                    const bool white = qRed(xorRow[x]) > 127;
                    // Инверсия (маска 1 + XOR 1) честно не воспроизводится —
                    // рисуем такие пиксели белым, так делают и системные темы.
                    dst[x] = keepOut && !white ? qRgba(0, 0, 0, 0)
                           : white             ? qRgba(255, 255, 255, 255)
                                               : qRgba(0, 0, 0, 255);
                }
            }
            out.image = img;
        }
    }

    ReleaseDC(nullptr, dc);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return out;
}

struct MonitorInfo {
    QString device;
    QRect rect;
};

BOOL CALLBACK collectMonitors(HMONITOR mon, HDC, LPRECT, LPARAM param) {
    auto* list = reinterpret_cast<QList<MonitorInfo>*>(param);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        list->append({ QString::fromWCharArray(mi.szDevice),
                       QRect(mi.rcMonitor.left, mi.rcMonitor.top,
                             mi.rcMonitor.right - mi.rcMonitor.left,
                             mi.rcMonitor.bottom - mi.rcMonitor.top) });
    }
    return TRUE;
}

} // namespace

ScreenCursor ScreenCursor::grab() {
    ScreenCursor out;
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor) return out;

    const quintptr key = reinterpret_cast<quintptr>(ci.hCursor);
    {
        QMutexLocker lock(&g_lock);
        auto it = g_cache.constFind(key);
        if (it != g_cache.constEnd()) {
            out.image = it->image;
            out.hotspot = it->hotspot;
        }
    }
    if (out.image.isNull()) {
        const Cached c = renderCursor(ci.hCursor);
        if (c.image.isNull()) return out;
        QMutexLocker lock(&g_lock);
        if (g_cache.size() > 32) g_cache.clear();   // формы не копим бесконечно
        g_cache.insert(key, c);
        out.image = c.image;
        out.hotspot = c.hotspot;
    }
    out.posPhysical = QPoint(ci.ptScreenPos.x, ci.ptScreenPos.y);
    return out;
}

// Физический прямоугольник монитора, на котором живёт этот QScreen.
//
// Раньше здесь СРАВНИВАЛИСЬ ИМЕНА, а при неудаче монитор подбирался «похожий»
// по размеру и положению. На одном мониторе это работало случайно — там ответ
// один и угадывать нечего. На двух ошибка вылезала целиком: имя QScreen на
// Windows — дружелюбное название из EDID, а не \\.\DISPLAY1, совпасть оно не
// может, и дальше выбирался монитор, чей размер ближе. При наборе «FHD + 4К»
// это давало прямоугольник 1920×1080 для телевизора 3840×2160 — то есть
// вдвое заниженный масштаб, отчего курсор в демонстрации рисовался ВДВОЕ
// КРУПНЕЕ настоящего и не на своём месте.
//
// Угадывать не нужно: платформенный плагин Qt хранит HMONITOR каждого экрана.
// Публичного доступа к нему нет, поэтому берём через QPlatformNativeInterface —
// как и хендл окна в ScreenSources, по той же причине и с тем же риском
// (мы собираемся против конкретной версии Qt).
QRect ScreenCursor::monitorRect(const QScreen* screen) {
    if (!screen) return {};

    if (QPlatformNativeInterface* ni = QGuiApplication::platformNativeInterface()) {
        // const_cast: интерфейс просит неконстантный QScreen, хотя ничего в нём
        // не меняет — читает хендл из платформенных данных.
        void* h = ni->nativeResourceForScreen(QByteArrayLiteral("handle"),
                                              const_cast<QScreen*>(screen));
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (h && GetMonitorInfoW(static_cast<HMONITOR>(h), &mi))
            return QRect(mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top);
    }

    // Резерв на случай, если Qt однажды перестанет отдавать хендл: единственный
    // монитор — единственный ответ. Подбирать «похожий» из нескольких мы больше
    // НЕ пытаемся: неверный масштаб курсора выглядит как поломка, а пустой
    // прямоугольник просто отключает его дорисовку — это честнее.
    QList<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitors,
                        reinterpret_cast<LPARAM>(&monitors));
    if (monitors.size() == 1) return monitors.first().rect;
    for (const MonitorInfo& m : monitors)
        if (m.device == screen->name()) return m.rect;
    return {};
}

#else  // не Windows

ScreenCursor ScreenCursor::grab() { return {}; }
QRect ScreenCursor::monitorRect(const QScreen*) { return {}; }

#endif
