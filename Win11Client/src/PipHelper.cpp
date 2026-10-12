#include "PipHelper.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <cmath>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <commctrl.h>
#  include <dwmapi.h>
#endif

namespace {
#ifdef Q_OS_WIN
// Атрибуты DWM из SDK Windows 11. Объявлены здесь, а не взяты из dwmapi.h:
// у старого SDK их нет, а на Windows 10 вызов просто вернёт ошибку — окно
// останется прямоугольным, и это честный откат, а не поломка.
constexpr DWORD kDwmCornerPreference = 33;   // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD kDwmCornerRound = 2;         // DWMWCP_ROUND
constexpr DWORD kDwmBorderColor = 34;        // DWMWA_BORDER_COLOR

// Номер нашего подкласса у окна (SetWindowSubclass различает подклассы по
// паре «процедура + номер»).
constexpr UINT_PTR kSubclassId = 0x50495031;   // 'PIP1'

// Минимальный размер окна, логические пиксели. Совпадает с minimumWidth и
// minimumHeight в PipWindow.qml: меньше этого кнопки поверх кадра уже не
// помещаются, а сам кадр перестаёт что-либо показывать.
constexpr int kMinW = 240;
constexpr int kMinH = 135;

// Магнит: окно ближе этого к краю рабочей области прилипает к отступу.
constexpr int kSnapPx = 20;
constexpr int kMarginPx = 16;

qreal scaleOf(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return dpi ? dpi / 96.0 : 1.0;
}
#endif
} // namespace

#ifdef Q_OS_WIN
struct PipSubclass {
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                 UINT_PTR id, DWORD_PTR data) {
        auto* self = reinterpret_cast<PipHelper*>(data);
        qintptr result = 0;
        if (self && self->handleMessage(reinterpret_cast<quintptr>(hwnd), msg,
                                        quintptr(wParam), qintptr(lParam), &result))
            return LRESULT(result);
        if (msg == WM_NCDESTROY) {
            // Окно умирает — снять подкласс обязательно: иначе система держит
            // указатель на процедуру, которая может пережить хелпер.
            RemoveWindowSubclass(hwnd, &PipSubclass::proc, id);
            if (self) self->m_windows.remove(reinterpret_cast<quintptr>(hwnd));
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
};
#endif

PipHelper::PipHelper(QObject* parent) : QObject(parent) {}

PipHelper::~PipHelper() {
#ifdef Q_OS_WIN
    // Окна «картинки в картинке» живут меньше хелпера (он на стеке main), но
    // если какое-то пережило — пусть не зовёт процедуру мёртвого объекта.
    for (auto it = m_windows.cbegin(); it != m_windows.cend(); ++it)
        RemoveWindowSubclass(reinterpret_cast<HWND>(it.key()), &PipSubclass::proc, kSubclassId);
#endif
}

void PipHelper::attach(QWindow* w) {
    if (!w) return;
#ifdef Q_OS_WIN
    // winId() создаёт нативное окно, если его ещё нет: атрибуты DWM нужно
    // выставить до первого показа, иначе окно мелькнёт прямоугольным.
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (!hwnd) return;

    // Скругление Windows 11. Окно без рамки система по умолчанию не скругляет:
    // для неё это «всплывашка», а не окно. Просьба явная — и DWM скругляет его
    // так же, как обычные окна (проверено пробой scratchpad/piptest).
    const DWORD corner = kDwmCornerRound;
    DwmSetWindowAttribute(hwnd, kDwmCornerPreference, &corner, sizeof(corner));
    // Рамку — в тон тёмного фона окна: системная светлая обводка вокруг
    // видео читается как выделение, а не как край.
    const COLORREF border = RGB(0x33, 0x33, 0x3a);
    DwmSetWindowAttribute(hwnd, kDwmBorderColor, &border, sizeof(border));

    const quintptr key = reinterpret_cast<quintptr>(hwnd);
    Entry& e = m_windows[key];
    e.window = w;
    // Повторный вызов с тем же номером лишь обновляет данные подкласса.
    SetWindowSubclass(hwnd, &PipSubclass::proc, kSubclassId, reinterpret_cast<DWORD_PTR>(this));
#endif
}

void PipHelper::detach(QWindow* w) {
    if (!w) return;
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        if (it->window == w || it->window.isNull()) {
#ifdef Q_OS_WIN
            RemoveWindowSubclass(reinterpret_cast<HWND>(it.key()), &PipSubclass::proc, kSubclassId);
#endif
            it = m_windows.erase(it);
        } else {
            ++it;
        }
    }
}

void PipHelper::setAspect(QWindow* w, qreal aspect) {
    if (!w) return;
    for (auto it = m_windows.begin(); it != m_windows.end(); ++it) {
        if (it->window == w) {
            it->aspect = aspect > 0.1 && aspect < 10 ? aspect : 0;
            return;
        }
    }
}

QRect PipHelper::workArea(QWindow* w) const {
    QScreen* s = w && w->screen() ? w->screen() : QGuiApplication::primaryScreen();
    return s ? s->availableGeometry() : QRect();
}

QRect PipHelper::workAreaAt(int x, int y) const {
    QScreen* s = QGuiApplication::screenAt(QPoint(x, y));
    return s ? s->availableGeometry() : QRect();
}

void PipHelper::bringToFront(QWindow* w) {
    if (!w) return;
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    // SW_RESTORE возвращает окно в то состояние, из которого его свернули:
    // развёрнутое — развёрнутым. QWindow::showNormal() сделал бы его обычным.
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    // Передний план Windows отдаёт только процессу, который получил последний
    // ввод. Это как раз наш случай: человек только что нажал кнопку в окне
    // «картинки в картинке», то есть ввод пришёл нам.
    SetForegroundWindow(hwnd);
#endif
    w->raise();
    w->requestActivate();
}

bool PipHelper::handleMessage(quintptr hwndKey, unsigned message, quintptr wParam,
                              qintptr lParam, qintptr* result) {
#ifdef Q_OS_WIN
    const auto it = m_windows.find(hwndKey);
    if (it == m_windows.end()) return false;
    const HWND hwnd = reinterpret_cast<HWND>(hwndKey);

    switch (message) {
    case WM_SIZING: {
        // Пропорции держит сам системный цикл растягивания: правим
        // предложенный им прямоугольник ДО того, как окно его примет. Тогда
        // окно тянется уже в нужной форме — без полос по краям кадра и без
        // «доворота» после отпускания.
        const qreal a = it->aspect;
        if (a <= 0) return false;
        RECT& r = *reinterpret_cast<RECT*>(lParam);
        const qreal k = scaleOf(hwnd);
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        const int edge = int(wParam);

        switch (edge) {
        case WMSZ_LEFT:
        case WMSZ_RIGHT:
            h = int(std::lround(w / a));           // тянут вбок — ведёт ширина
            break;
        case WMSZ_TOP:
        case WMSZ_BOTTOM:
            w = int(std::lround(h * a));           // тянут вверх-вниз — высота
            break;
        default: {
            // Угол: ведёт та сторона, которую потянули сильнее. Иначе мелкое
            // движение по вертикали при широком жесте вбок дёргало бы окно.
            RECT cur{};
            GetWindowRect(hwnd, &cur);
            const qreal cw = qMax<LONG>(1, cur.right - cur.left);
            const qreal ch = qMax<LONG>(1, cur.bottom - cur.top);
            if (std::abs(w - cw) / cw >= std::abs(h - ch) / ch)
                h = int(std::lround(w / a));
            else
                w = int(std::lround(h * a));
            break;
        }
        }

        // Нижняя граница — с сохранением пропорций: упёрлись в минимум по
        // одной стороне — вторая подтягивается следом.
        const int minW = int(std::lround(kMinW * k));
        const int minH = int(std::lround(kMinH * k));
        if (w < minW) { w = minW; h = int(std::lround(w / a)); }
        if (h < minH) { h = minH; w = int(std::lround(h * a)); }

        // Верхняя — рабочая область монитора: окно, растянутое за экран,
        // нельзя ни увидеть целиком, ни ухватить за дальний край.
        HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
            const int maxW = mi.rcWork.right - mi.rcWork.left;
            const int maxH = mi.rcWork.bottom - mi.rcWork.top;
            if (w > maxW) { w = maxW; h = int(std::lround(w / a)); }
            if (h > maxH) { h = maxH; w = int(std::lround(h * a)); }
        }

        // Противоположные стороны стоят на месте — двигается только та, за
        // которую тянут (у бокового края — заодно нижняя).
        switch (edge) {
        case WMSZ_LEFT:        r.left = r.right - w;  r.bottom = r.top + h;  break;
        case WMSZ_RIGHT:       r.right = r.left + w;  r.bottom = r.top + h;  break;
        case WMSZ_TOP:         r.top = r.bottom - h;  r.right = r.left + w;  break;
        case WMSZ_BOTTOM:      r.bottom = r.top + h;  r.right = r.left + w;  break;
        case WMSZ_TOPLEFT:     r.left = r.right - w;  r.top = r.bottom - h;  break;
        case WMSZ_TOPRIGHT:    r.right = r.left + w;  r.top = r.bottom - h;  break;
        case WMSZ_BOTTOMLEFT:  r.left = r.right - w;  r.bottom = r.top + h;  break;
        case WMSZ_BOTTOMRIGHT: r.right = r.left + w;  r.bottom = r.top + h;  break;
        default: break;
        }
        *result = TRUE;
        return true;
    }

    case WM_MOVING: {
        // Магнит. Окно «картинки в картинке» почти всегда живёт в углу экрана,
        // и попасть в угол с точностью до пикселя мышью незачем: ближе 20 px к
        // краю — встаёт ровно на отступ. Дальше — едет свободно, и за край его
        // вытолкнуть тоже можно (магнит не держит, если тянуть дальше).
        RECT& r = *reinterpret_cast<RECT*>(lParam);
        HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(mon, &mi)) return false;
        const RECT wa = mi.rcWork;
        const qreal k = scaleOf(hwnd);
        const int snap = int(std::lround(kSnapPx * k));
        const int margin = int(std::lround(kMarginPx * k));
        const int w = r.right - r.left;
        const int h = r.bottom - r.top;
        const auto nearEdge = [snap](LONG a, LONG b) { return std::abs(a - b) <= snap; };

        if (nearEdge(r.left, wa.left + margin))          { r.left = wa.left + margin;     r.right = r.left + w; }
        else if (nearEdge(r.right, wa.right - margin))   { r.right = wa.right - margin;   r.left = r.right - w; }
        if (nearEdge(r.top, wa.top + margin))            { r.top = wa.top + margin;       r.bottom = r.top + h; }
        else if (nearEdge(r.bottom, wa.bottom - margin)) { r.bottom = wa.bottom - margin; r.top = r.bottom - h; }
        *result = TRUE;
        return true;
    }

    case WM_EXITSIZEMOVE:
        // Место и размер — запомнить. Сообщение не съедаем: Qt тоже его ждёт.
        if (it->window) emit moveFinished(it->window);
        return false;

    default:
        return false;
    }
#else
    Q_UNUSED(hwndKey) Q_UNUSED(message) Q_UNUSED(wParam) Q_UNUSED(lParam) Q_UNUSED(result)
    return false;
#endif
}
