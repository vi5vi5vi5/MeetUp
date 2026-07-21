#include "ScreenCursor.h"
#include <QHash>
#include <QMutex>
#include <QString>
#include <QList>
#include <QScreen>

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

// Физический прямоугольник монитора. Имя QScreen на Windows — это дружелюбное
// название из EDID («TV-Station»), а вовсе не \\.\DISPLAY1, поэтому сходу
// сопоставить его с монитором Windows нельзя (проверено пробой). Сначала
// пробуем имя — вдруг совпадёт, — потом единственный монитор, потом
// подбираем по размеру и положению.
QRect ScreenCursor::monitorRect(const QScreen* screen) {
    if (!screen) return {};
    QList<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitors,
                        reinterpret_cast<LPARAM>(&monitors));
    if (monitors.isEmpty()) return {};
    if (monitors.size() == 1) return monitors.first().rect;

    for (const MonitorInfo& m : monitors)
        if (m.device == screen->name()) return m.rect;

    const qreal dpr = screen->devicePixelRatio();
    const QRect want(QPoint(qRound(screen->geometry().x() * dpr),
                            qRound(screen->geometry().y() * dpr)),
                     QSize(qRound(screen->geometry().width() * dpr),
                           qRound(screen->geometry().height() * dpr)));
    QRect best;
    qint64 bestScore = -1;
    for (const MonitorInfo& m : monitors) {
        const qint64 score = qAbs(m.rect.width() - want.width())
                           + qAbs(m.rect.height() - want.height())
                           + qAbs(m.rect.x() - want.x()) / 4
                           + qAbs(m.rect.y() - want.y()) / 4;
        if (bestScore < 0 || score < bestScore) { bestScore = score; best = m.rect; }
    }
    return best;
}

#else  // не Windows

ScreenCursor ScreenCursor::grab() { return {}; }
QRect ScreenCursor::monitorRect(const QScreen*) { return {}; }

#endif
