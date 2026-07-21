#include "GlobalHotkeys.h"
#include "MediaSettings.h"
#include <QGuiApplication>
#include <QKeySequence>
#include <QKeyCombination>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

// Идентификаторы клавиш = индекс + 1 (0 у RegisterHotKey зарезервирован).
enum { IdMic = 1, IdSound = 2, IdCam = 3 };

#ifdef Q_OS_WIN
// Qt::Key -> виртуальный код Windows. Буквы и цифры у Qt совпадают с ASCII (а
// значит и с VK), функциональные — своим блоком. ВАЖНО: клавиши нумпада надо
// брать по флагу Qt::KeypadModifier и отдавать VK_NUMPAD*, иначе «Num 5»
// зарегистрируется как обычная «5» (VK_5) и не сработает — на этом бинды и
// молчали.
static UINT qtKeyToVk(int key, Qt::KeyboardModifiers mods) {
    if (mods & Qt::KeypadModifier) {
        if (key >= Qt::Key_0 && key <= Qt::Key_9) return UINT(VK_NUMPAD0 + (key - Qt::Key_0));
        switch (key) {
        case Qt::Key_Asterisk: return VK_MULTIPLY;
        case Qt::Key_Plus:     return VK_ADD;
        case Qt::Key_Minus:    return VK_SUBTRACT;
        case Qt::Key_Period:   return VK_DECIMAL;
        case Qt::Key_Slash:    return VK_DIVIDE;
        case Qt::Key_Enter:    return VK_RETURN;
        default: break;                     // навигация нумпада (NumLock off) — ниже
        }
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return UINT('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return UINT('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return UINT(VK_F1 + (key - Qt::Key_F1));
    switch (key) {
    case Qt::Key_Space:    return VK_SPACE;
    case Qt::Key_Home:     return VK_HOME;
    case Qt::Key_End:      return VK_END;
    case Qt::Key_Insert:   return VK_INSERT;
    case Qt::Key_Delete:   return VK_DELETE;
    case Qt::Key_PageUp:   return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Pause:    return VK_PAUSE;
    default: return 0;
    }
}
#endif

GlobalHotkeys::GlobalHotkeys(MediaSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings) {
#ifdef Q_OS_WIN
    qApp->installNativeEventFilter(this);
#endif
    connect(settings, &MediaSettings::keyMicChanged,   this, &GlobalHotkeys::reregister);
    connect(settings, &MediaSettings::keySoundChanged, this, &GlobalHotkeys::reregister);
    connect(settings, &MediaSettings::keyCamChanged,   this, &GlobalHotkeys::reregister);
}

GlobalHotkeys::~GlobalHotkeys() {
    unregisterAll();
#ifdef Q_OS_WIN
    qApp->removeNativeEventFilter(this);
#endif
}

void GlobalHotkeys::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    reregister();
}

void GlobalHotkeys::unregisterAll() {
#ifdef Q_OS_WIN
    for (int i = 0; i < 3; ++i)
        if (m_registered[i]) { UnregisterHotKey(nullptr, i + 1); m_registered[i] = false; }
#endif
}

void GlobalHotkeys::reregister() {
    unregisterAll();
    if (!m_active) return;
    const QString seqs[3] = {
        m_settings->keyMic(), m_settings->keySound(), m_settings->keyCam() };
    for (int i = 0; i < 3; ++i) {
        if (seqs[i].isEmpty()) continue;
        if (registerOne(i + 1, seqs[i])) m_registered[i] = true;
        else emit conflict(seqs[i]);          // клавишу держит другой процесс
    }
}

bool GlobalHotkeys::registerOne(int id, const QString& seqText) {
#ifdef Q_OS_WIN
    const QKeySequence seq(seqText, QKeySequence::PortableText);
    if (seq.isEmpty()) return false;
    const QKeyCombination kc = seq[0];
    const int key = kc.key();
    const Qt::KeyboardModifiers mods = kc.keyboardModifiers();

    const UINT vk = qtKeyToVk(key, mods);
    if (!vk) return false;

    // Keypad — не системный модификатор Windows (его роль уже сыграл выбор
    // VK_NUMPAD*), поэтому в winMods идут только настоящие модификаторы.
    UINT winMods = MOD_NOREPEAT;              // удержание не должно частить
    if (mods & Qt::ControlModifier) winMods |= MOD_CONTROL;
    if (mods & Qt::AltModifier)     winMods |= MOD_ALT;
    if (mods & Qt::ShiftModifier)   winMods |= MOD_SHIFT;
    if (mods & Qt::MetaModifier)    winMods |= MOD_WIN;

    // Модификатор НЕ обязателен: пользователь вправе повесить действие на одну
    // клавишу, которую больше нигде не жмёт (нумпад, F-клавиша и т.п.).
    return RegisterHotKey(nullptr, id, winMods, vk);
#else
    Q_UNUSED(id); Q_UNUSED(seqText);
    return false;
#endif
}

bool GlobalHotkeys::nativeEventFilter(const QByteArray& type, void* message, qintptr*) {
#ifdef Q_OS_WIN
    if (type != "windows_generic_MSG") return false;
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_HOTKEY) {
        switch (int(msg->wParam)) {
        case IdMic:   emit micHotkey();   break;
        case IdSound: emit soundHotkey(); break;
        case IdCam:   emit camHotkey();   break;
        default: break;
        }
    }
#else
    Q_UNUSED(type); Q_UNUSED(message);
#endif
    return false;   // не поглощаем — пусть система делает своё
}
