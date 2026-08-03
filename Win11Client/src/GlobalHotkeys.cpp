#include "GlobalHotkeys.h"
#include "MediaSettings.h"
#include <QGuiApplication>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

const wchar_t* kSinkClass = L"MeetUpHotkeySink";

LRESULT CALLBACK sinkWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    } else if (msg == WM_INPUT) {
        auto* self = reinterpret_cast<GlobalHotkeys*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) self->handleRawInput(reinterpret_cast<void*>(lp));
        // WM_INPUT обязан дойти до DefWindowProc: на нём система освобождает
        // свой буфер под событие.
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

GlobalHotkeys::GlobalHotkeys(MediaSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings) {
    m_clock.start();
    connect(settings, &MediaSettings::keyMicChanged,   this, &GlobalHotkeys::reloadBindings);
    connect(settings, &MediaSettings::keySoundChanged, this, &GlobalHotkeys::reloadBindings);
    connect(settings, &MediaSettings::keyCamChanged,   this, &GlobalHotkeys::reloadBindings);
    reloadBindings();
}

GlobalHotkeys::~GlobalHotkeys() {
    stopListening();
    if (m_hwnd) DestroyWindow(HWND(m_hwnd));
}

void GlobalHotkeys::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    refreshListener();
}

void GlobalHotkeys::startCapture() {
    if (m_capture) return;
    m_capture = true;
    resetChord();
    refreshListener();
}

void GlobalHotkeys::stopCapture() {
    if (!m_capture) return;
    m_capture = false;
    resetChord();
    refreshListener();
}

QString GlobalHotkeys::label(const QString& text) const {
    return HotkeySpec::display(text);
}

void GlobalHotkeys::reloadBindings() {
    m_specs[0] = HotkeySpec::parse(m_settings->keyMic());
    m_specs[1] = HotkeySpec::parse(m_settings->keySound());
    m_specs[2] = HotkeySpec::parse(m_settings->keyCam());
    refreshListener();
}

// ---------- приёмник сырого ввода ----------

void GlobalHotkeys::refreshListener() {
    const bool anyBound = !(m_specs[0].isEmpty() && m_specs[1].isEmpty() && m_specs[2].isEmpty());
    // Вне конференции и без единого бинда слушать нечего: чужие нажатия нам
    // тогда не нужны, и просить их у системы незачем.
    if (m_capture || (m_active && anyBound)) startListening();
    else stopListening();
}

bool GlobalHotkeys::startListening() {
    if (m_listening) return true;

    if (!m_hwnd) {
        const HINSTANCE inst = GetModuleHandleW(nullptr);
        static bool classReady = false;
        if (!classReady) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = sinkWndProc;
            wc.hInstance = inst;
            wc.lpszClassName = kSinkClass;
            if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                qWarning("GlobalHotkeys: не создать класс окна-приёмника (%lu)", GetLastError());
                return false;
            }
            classReady = true;
        }
        // Приёмнику сырого ввода нужен адресат-окно, но показывать нечего:
        // HWND_MESSAGE даёт окно без экрана, без оформления и без места в
        // Alt+Tab — оно существует только ради очереди сообщений.
        m_hwnd = CreateWindowExW(0, kSinkClass, L"MeetUp hotkeys", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, inst, this);
        if (!m_hwnd) {
            qWarning("GlobalHotkeys: не создать окно-приёмник (%lu)", GetLastError());
            return false;
        }
    }

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;            // generic desktop
    rid.usUsage = 0x06;                // keyboard
    // INPUTSINK — «отдавай и когда окно не в фокусе»; ради этого всё и затеяно.
    // Клавиши при этом остаются общими: копия события достаётся нам, оригинал
    // как шёл в активную программу, так и идёт.
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = HWND(m_hwnd);
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        qWarning("GlobalHotkeys: не подписаться на сырой ввод (%lu)", GetLastError());
        return false;
    }
    m_listening = true;
    resetChord();
    return true;
}

void GlobalHotkeys::stopListening() {
    if (!m_listening) return;
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x06;
    rid.dwFlags = RIDEV_REMOVE;
    rid.hwndTarget = nullptr;          // при снятии подписки обязан быть пуст
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    m_listening = false;
    resetChord();
}

void GlobalHotkeys::resetChord() {
    m_modsDown = 0;
    m_chordPeak = 0;
    m_chordDirty = false;
    m_keysDown.clear();
    m_ctrlDownBit = 0;
}

void GlobalHotkeys::handleRawInput(void* rawInputHandle) {
    RAWINPUT ri{};
    UINT size = sizeof(ri);
    if (GetRawInputData(HRAWINPUT(rawInputHandle), RID_INPUT, &ri, &size,
                        sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
    if (ri.header.dwType != RIM_TYPEKEYBOARD) return;
    const RAWKEYBOARD& kb = ri.data.keyboard;

    // 0xFF драйвер шлёт под собственные нужды (так помечена, например, добавка
    // к Ctrl+NumLock) — это не клавиша.
    if (kb.VKey == 0xFF) return;

    quint32 vk = kb.VKey;
    const bool e0 = (kb.Flags & RI_KEY_E0) != 0;
    const bool up = (kb.Flags & RI_KEY_BREAK) != 0;

    // Сторона клавиши. Общие VK_SHIFT/VK_CONTROL/VK_MENU разводим сами: правую
    // половину клавиатуры помечает префикс E0, а у Shift сторону знает только
    // скан-код (0x2A слева, 0x36 справа) — его и спрашиваем.
    switch (vk) {
    case VK_SHIFT: {
        const UINT sided = MapVirtualKeyW(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
        vk = sided ? sided : VK_LSHIFT;
        break;
    }
    case VK_CONTROL: vk = e0 ? VK_RCONTROL : VK_LCONTROL; break;
    case VK_MENU:    vk = e0 ? VK_RMENU : VK_LMENU; break;
    default: break;
    }

    // Нумпад. При выключенном NumLock его клавиши приходят кодами навигации, и
    // отличить их от настоящих Home/End можно ровно одним признаком: у
    // настоящих есть префикс E0, у нумпада его нет. Без этого бинд на «Num 7»
    // срабатывал бы и на Home.
    if (!e0) {
        switch (vk) {
        case VK_HOME:   vk = VK_NUMPAD7; break;
        case VK_UP:     vk = VK_NUMPAD8; break;
        case VK_PRIOR:  vk = VK_NUMPAD9; break;
        case VK_LEFT:   vk = VK_NUMPAD4; break;
        case VK_CLEAR:  vk = VK_NUMPAD5; break;
        case VK_RIGHT:  vk = VK_NUMPAD6; break;
        case VK_END:    vk = VK_NUMPAD1; break;
        case VK_DOWN:   vk = VK_NUMPAD2; break;
        case VK_NEXT:   vk = VK_NUMPAD3; break;
        case VK_INSERT: vk = VK_NUMPAD0; break;
        case VK_DELETE: vk = VK_DECIMAL; break;
        default: break;
        }
    } else if (vk == VK_RETURN) {
        vk = VK_SEPARATOR;               // Enter на нумпаде (см. HotkeySpec.cpp)
    }

    // Клавиши, которыми управляют самим назначением (отмена, очистка) и полным
    // экраном: их разбирает QML, назначать их биндом нельзя.
    if (m_capture && (vk == VK_ESCAPE || vk == VK_BACK || vk == VK_DELETE || vk == VK_F11))
        return;

    onKey(vk, !up);
}

// ---------- разбор аккорда ----------

void GlobalHotkeys::onKey(quint32 vk, bool down) {
    const quint32 modBit = HotkeySpec::modBitForVk(vk);

    if (modBit) {
        if (!down) {
            m_modsDown &= ~modBit;
            if (modBit == m_ctrlDownBit) m_ctrlDownBit = 0;
            if (m_modsDown == 0) endChord();
            return;
        }
        if (m_modsDown & modBit) return;        // автоповтор зажатого модификатора

        // Раскладки с AltGr (US-International, немецкая и прочие) шлют перед
        // правым Alt фальшивый Ctrl. Он неотличим от настоящего ничем, кроме
        // времени: приходит тем же пакетом ввода, то есть в ту же миллисекунду.
        // Не убрать его — и бинд на правом Alt не сработает никогда.
        if (vk == VK_RMENU && m_ctrlDownBit && m_clock.elapsed() - m_ctrlDownAt <= 2) {
            m_modsDown &= ~m_ctrlDownBit;
            m_chordPeak &= ~m_ctrlDownBit;
            m_ctrlDownBit = 0;
        }
        if (modBit == HotkeySpec::LCtrl || modBit == HotkeySpec::RCtrl) {
            m_ctrlDownBit = modBit;
            m_ctrlDownAt = m_clock.elapsed();
        }
        m_modsDown |= modBit;
        m_chordPeak |= modBit;
        return;
    }

    if (!down) { m_keysDown.remove(vk); return; }

    // Обычная клавиша портит аккорд из модификаторов: RCtrl+C — это копирование,
    // а не бинд на правом Ctrl.
    m_chordDirty = true;
    if (m_keysDown.contains(vk)) return;        // автоповтор зажатой клавиши
    m_keysDown.insert(vk);

    if (m_capture) {
        HotkeySpec spec;
        // Сторону у сочетания не запоминаем: Ctrl+D должен работать любым Ctrl.
        spec.mods = HotkeySpec::anySide(m_modsDown);
        spec.vk = vk;
        finishCapture(spec);
        return;
    }
    if (!m_active || typingInOurWindow()) return;

    HotkeySpec pressed;
    pressed.mods = m_modsDown;
    pressed.vk = vk;
    fire(pressed);
}

void GlobalHotkeys::endChord() {
    const quint32 peak = m_chordPeak;
    const bool dirty = m_chordDirty;
    m_chordPeak = 0;
    m_chordDirty = false;
    if (!peak) return;

    if (m_capture) {
        // Аккорд с обычной клавишей захват уже закончил на её нажатии — сюда
        // доходят только чистые сочетания модификаторов.
        if (!dirty) {
            HotkeySpec spec;
            spec.mods = peak;
            finishCapture(spec);
        }
        return;
    }
    if (dirty || !m_active) return;

    HotkeySpec pressed;
    pressed.mods = peak;              // vk == 0 — бинд из одних модификаторов
    fire(pressed);
}

void GlobalHotkeys::fire(const HotkeySpec& pressed) {
    for (int i = 0; i < 3; ++i) {
        const HotkeySpec& s = m_specs[i];
        if (s.isEmpty() || s.vk != pressed.vk) continue;
        if (!s.modsMatch(pressed.mods)) continue;
        switch (i) {
        case 0: emit micHotkey();   break;
        case 1: emit soundHotkey(); break;
        case 2: emit camHotkey();   break;
        }
        return;                       // одно нажатие — одно действие
    }
}

void GlobalHotkeys::finishCapture(const HotkeySpec& spec) {
    const QString text = spec.toText();
    // Клавиша, которую мы не умеем назвать, биндом стать не может — ждём
    // следующую, а не выходим из назначения молча.
    if (text.isEmpty()) return;
    stopCapture();
    emit captured(text);
}

bool GlobalHotkeys::typingInOurWindow() const {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return false;
    const QObject* focus = qGuiApp ? qGuiApp->focusObject() : nullptr;
    // Раз клавиша больше не изымается из системы, бинд на букве срабатывал бы
    // прямо во время набора — и в чате, и в любом другом поле. Пока курсор
    // стоит в тексте, обычные клавиши принадлежат тексту.
    return focus && (focus->inherits("QQuickTextInput") || focus->inherits("QQuickTextEdit"));
}
