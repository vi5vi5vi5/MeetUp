#include "ScreenSources.h"
#include <QGuiApplication>
#include <QScreen>
#include <QPixmap>
#include <QWindowCapture>
#include <QtMultimedia/private/qcapturablewindow_p.h>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <dwmapi.h>
#  ifndef PW_RENDERFULLCONTENT
#    define PW_RENDERFULLCONTENT 0x00000002
#  endif
#endif

// Габариты миниатюры (карточка в модалке ~260 пикселей — с запасом на HiDPI).
static const QSize kThumbBox(480, 270);

#ifdef Q_OS_WIN

// HWND окна, которое перечислил Qt. Публичного доступа к нему нет, но без
// хендла нельзя ни отсеять мусор, ни снять предпросмотр, поэтому берём его из
// приватной структуры (мы собираемся ровно против этой версии Qt).
static HWND handleOf(const QCapturableWindow& w) {
    const QCapturableWindowPrivate* d = QCapturableWindowPrivate::handle(w);
    return d ? reinterpret_cast<HWND>(d->id) : nullptr;
}

// «Скрытое» окно в терминах DWM. Именно так выглядят призраки приостановленных
// UWP-приложений (Параметры, Калькулятор, панель ввода): окно есть, оно даже
// «видимое» по IsWindowVisible, но на экране его нет и захватывать нечего.
static bool cloaked(HWND h) {
    BOOL flag = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &flag, sizeof(flag))))
        return flag != FALSE;
    return false;
}

// Годится ли окно к показу в списке. Список Qt берёт всё подряд, включая
// служебные слои оболочки, — фильтр наш.
static bool usable(HWND h, QSize* sizeOut) {
    if (!h || !IsWindow(h) || !IsWindowVisible(h) || IsIconic(h)) return false;
    if (cloaked(h)) return false;
    // Панели, всплывающие подсказки и прочее, чего нет в Alt+Tab.
    if (GetWindowLongPtr(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;

    RECT r{};
    if (!GetWindowRect(h, &r)) return false;
    const int w = r.right - r.left, ht = r.bottom - r.top;
    if (w < 200 || ht < 120) return false;      // огрызки размером с иконку
    if (sizeOut) *sizeOut = QSize(w, ht);
    return true;
}

// Снимок окна. PrintWindow с PW_RENDERFULLCONTENT просит само окно
// перерисоваться в наш DC — работает и когда окно перекрыто другим.
static QImage grabWindow(HWND h) {
    RECT r{};
    if (!GetWindowRect(h, &r)) return {};
    const int w = r.right - r.left, ht = r.bottom - r.top;
    if (w <= 0 || ht <= 0) return {};

    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -ht;                 // минус = строки сверху вниз
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    QImage out;
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(memDc, bmp);
        if (PrintWindow(h, memDc, PW_RENDERFULLCONTENT)) {
            // Альфа у PrintWindow бывает нулевой — берём формат без неё, иначе
            // предпросмотр вышел бы полностью прозрачным.
            const QImage view(static_cast<const uchar*>(bits), w, ht,
                              w * 4, QImage::Format_RGB32);
            out = view.scaled(kThumbBox, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        SelectObject(memDc, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return out;
}

#endif // Q_OS_WIN

ScreenSources::ScreenSources(QObject* parent) : QObject(parent) {
    refresh();
}

void ScreenSources::refresh() {
    ++m_nonce;
    m_thumbs.clear();          // иначе за исчезнувшим источником тянулся бы старый кадр
    rebuildScreens();
    rebuildWindows();
    emit listsChanged();

    // Выбранный источник мог исчезнуть (монитор отключили, окно закрыли).
    if (!m_selKey.isEmpty()) {
        bool alive = false;
        for (const QVariant& v : m_screens) if (v.toMap().value("key") == m_selKey) alive = true;
        for (const QVariant& v : m_windows) if (v.toMap().value("key") == m_selKey) alive = true;
        if (!alive) select(QString());
    }
}

QString ScreenSources::thumbUrl(const QString& key) const {
    return m_thumbs.contains(key)
        ? QString("image://screengrab/%1/%2").arg(key).arg(m_nonce)
        : QString();
}

void ScreenSources::rebuildScreens() {
    m_screens.clear();
    const QList<QScreen*> list = QGuiApplication::screens();
    for (int i = 0; i < list.size(); ++i) {
        QScreen* s = list.at(i);
        const QString key = "screen:" + QString::number(i);
        const QSize px = s->geometry().size() * s->devicePixelRatio();

        // Снимок целого монитора: grabWindow(0) у QScreen — это его рабочий стол.
        const QPixmap shot = s->grabWindow(0);
        if (!shot.isNull())
            m_thumbs.insert(key, shot.toImage().scaled(
                kThumbBox, Qt::KeepAspectRatio, Qt::SmoothTransformation));

        QVariantMap m;
        m["key"] = key;
        m["label"] = list.size() > 1 ? tr("Экран %1").arg(i + 1) : tr("Весь экран");
        m["sub"] = QString("%1 × %2").arg(px.width()).arg(px.height());
        m["thumb"] = thumbUrl(key);
        m_screens.append(m);
    }
}

void ScreenSources::rebuildWindows() {
    m_windows.clear();
    m_winHandles.clear();
    const QString self = QGuiApplication::applicationDisplayName();
    for (const QCapturableWindow& w : QWindowCapture::capturableWindows()) {
        const QString title = w.description().trimmed();
        // Безымянные окна — служебные слои системы, показывать их незачем.
        if (!w.isValid() || title.isEmpty()) continue;
        // Своё же окно в списке — прямой путь к «бесконечному коридору».
        if (!self.isEmpty() && title.contains(self)) continue;

        QSize size;
#ifdef Q_OS_WIN
        HWND h = handleOf(w);
        // Хендл есть всегда (проверено пробой), но если Qt однажды сменит
        // внутренности — лучше показать окно без фильтра, чем пустой список.
        if (h && !usable(h, &size)) continue;    // мусор системы — мимо списка
#endif
        QVariantMap m;
        m["key"] = "window:" + QString::number(m_winHandles.size());
        m["label"] = title;
        m["sub"] = size.isValid() ? QString("%1 × %2").arg(size.width()).arg(size.height())
                                  : tr("Окно приложения");
        m["thumb"] = QString();                  // доснимем по refreshWindowThumbs
        m_windows.append(m);
        m_winHandles.append(w);
    }
}

void ScreenSources::refreshWindowThumbs() {
#ifdef Q_OS_WIN
    bool any = false;
    for (int i = 0; i < m_winHandles.size() && i < m_windows.size(); ++i) {
        QVariantMap m = m_windows.at(i).toMap();
        const QString key = m.value("key").toString();
        if (m_thumbs.contains(key)) continue;              // уже сняли
        HWND h = handleOf(m_winHandles.at(i));
        if (!h) continue;
        const QImage shot = grabWindow(h);
        if (shot.isNull()) continue;                       // окно не дало себя снять
        m_thumbs.insert(key, shot);
        m["thumb"] = thumbUrl(key);
        m_windows[i] = m;
        any = true;
    }
    if (any) emit listsChanged();
#endif
}

void ScreenSources::select(const QString& key) {
    QString label;
    if (!key.isEmpty()) {
        for (const QVariant& v : m_screens + m_windows) {
            const QVariantMap m = v.toMap();
            if (m.value("key").toString() == key) { label = m.value("label").toString(); break; }
        }
        if (label.isEmpty()) return;      // ключа больше нет — выбор не меняем
    }
    if (m_selKey == key && m_selLabel == label) return;
    m_selKey = key;
    m_selLabel = label;
    emit selectedChanged();
}

QScreen* ScreenSources::selectedScreen() const {
    if (!m_selKey.startsWith("screen:")) return nullptr;
    const int i = m_selKey.mid(7).toInt();
    const QList<QScreen*> list = QGuiApplication::screens();
    return (i >= 0 && i < list.size()) ? list.at(i) : nullptr;
}

QCapturableWindow ScreenSources::selectedWindow() const {
    if (!m_selKey.startsWith("window:")) return {};
    const int i = m_selKey.mid(7).toInt();
    return (i >= 0 && i < m_winHandles.size()) ? m_winHandles.at(i) : QCapturableWindow();
}

// id приходит как "<key>/<nonce>": nonce нужен лишь для того, чтобы QML
// перезапросил картинку после refresh(), в поиске по кэшу он не участвует.
QImage ScreenThumbProvider::requestImage(const QString& id, QSize* size,
                                         const QSize& requested) {
    QImage img = m_src->thumb(id.section('/', 0, 0));
    if (img.isNull()) return img;
    if (requested.width() > 0 && requested.width() < img.width())
        img = img.scaledToWidth(requested.width(), Qt::SmoothTransformation);
    if (size) *size = img.size();
    return img;
}
