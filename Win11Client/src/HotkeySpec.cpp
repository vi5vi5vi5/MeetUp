#include "HotkeySpec.h"
#include <QStringList>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

// Класс модификатора: бит «любой стороны» и две конкретные стороны. Порядок
// строк задаёт и порядок токенов в тексте бинда.
struct ModClass { quint32 any, left, right; const char* name; };
const ModClass kClasses[] = {
    { HotkeySpec::AnyCtrl,  HotkeySpec::LCtrl,  HotkeySpec::RCtrl,  "Ctrl"  },
    { HotkeySpec::AnyShift, HotkeySpec::LShift, HotkeySpec::RShift, "Shift" },
    { HotkeySpec::AnyAlt,   HotkeySpec::LAlt,   HotkeySpec::RAlt,   "Alt"   },
    { HotkeySpec::AnyWin,   HotkeySpec::LWin,   HotkeySpec::RWin,   "Win"   },
};

struct KeyName { const char* name; quint32 vk; };

// Канонические имена обычных клавиш. Буквы, цифры и F-клавиши сюда не входят —
// они считаются по формуле. Нумпад отделён от основной клавиатуры намеренно:
// «Num 5» и «5» — разные клавиши, и бинд на первой не должен мешать второй.
const KeyName kKeys[] = {
    { "Space", VK_SPACE }, { "Tab", VK_TAB }, { "Enter", VK_RETURN },
    { "Home", VK_HOME }, { "End", VK_END }, { "Insert", VK_INSERT },
    { "Delete", VK_DELETE }, { "PageUp", VK_PRIOR }, { "PageDown", VK_NEXT },
    { "Pause", VK_PAUSE }, { "ScrollLock", VK_SCROLL },
    { "Up", VK_UP }, { "Down", VK_DOWN }, { "Left", VK_LEFT }, { "Right", VK_RIGHT },
    { "Num0", VK_NUMPAD0 }, { "Num1", VK_NUMPAD1 }, { "Num2", VK_NUMPAD2 },
    { "Num3", VK_NUMPAD3 }, { "Num4", VK_NUMPAD4 }, { "Num5", VK_NUMPAD5 },
    { "Num6", VK_NUMPAD6 }, { "Num7", VK_NUMPAD7 }, { "Num8", VK_NUMPAD8 },
    { "Num9", VK_NUMPAD9 },
    { "NumMul", VK_MULTIPLY }, { "NumAdd", VK_ADD }, { "NumSub", VK_SUBTRACT },
    { "NumDec", VK_DECIMAL }, { "NumDiv", VK_DIVIDE },
    // У Enter на нумпаде своего виртуального кода в Windows нет вовсе —
    // от основного он отличается только префиксом E0 в скан-коде. Берём под
    // него VK_SEPARATOR: на обычных клавиатурах этой клавиши не существует,
    // так что занять её код безопасно (см. GlobalHotkeys::handleRawInput).
    { "NumEnter", VK_SEPARATOR },
};

// Написания, которые мы только читаем: так клавиши назывались в тексте
// QKeySequence, которым бинды сохраняла прежняя версия.
const KeyName kAliases[] = {
    { "Ins", VK_INSERT }, { "Del", VK_DELETE }, { "PgUp", VK_PRIOR },
    { "PgDown", VK_NEXT }, { "PgDn", VK_NEXT }, { "Return", VK_RETURN },
};

// Клавиша нумпада, соответствующая обычной. Нужна для старого текста вида
// "Num+5" и "Num+Home", где нумпад был отдельным модификатором.
quint32 toKeypad(quint32 vk) {
    if (vk >= '0' && vk <= '9') return VK_NUMPAD0 + (vk - '0');
    switch (vk) {
    case VK_RETURN: return VK_SEPARATOR;
    case VK_HOME:   return VK_NUMPAD7;
    case VK_UP:     return VK_NUMPAD8;
    case VK_PRIOR:  return VK_NUMPAD9;
    case VK_LEFT:   return VK_NUMPAD4;
    case VK_RIGHT:  return VK_NUMPAD6;
    case VK_END:    return VK_NUMPAD1;
    case VK_DOWN:   return VK_NUMPAD2;
    case VK_NEXT:   return VK_NUMPAD3;
    case VK_INSERT: return VK_NUMPAD0;
    case VK_DELETE: return VK_DECIMAL;
    default:        return vk;
    }
}

quint32 vkForKeyToken(const QString& t) {
    if (t.size() == 1) {
        const QChar c = t.at(0).toUpper();
        if (c >= 'A' && c <= 'Z') return quint32(c.unicode());
        if (c >= '0' && c <= '9') return quint32(c.unicode());
        return 0;
    }
    if (t.size() >= 2 && (t.at(0) == 'F' || t.at(0) == 'f')) {
        bool ok = false;
        const int n = t.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24) return VK_F1 + quint32(n - 1);
    }
    for (const KeyName& k : kKeys)
        if (t.compare(QLatin1String(k.name), Qt::CaseInsensitive) == 0) return k.vk;
    for (const KeyName& k : kAliases)
        if (t.compare(QLatin1String(k.name), Qt::CaseInsensitive) == 0) return k.vk;
    return 0;
}

QString keyTokenForVk(quint32 vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
        return QString(QChar(ushort(vk)));
    if (vk >= VK_F1 && vk <= VK_F24)
        return QStringLiteral("F%1").arg(vk - VK_F1 + 1);
    for (const KeyName& k : kKeys)
        if (k.vk == vk) return QLatin1String(k.name);
    return QString();
}

// Человекочитаемое имя клавиши. Отличается от канонического только там, где
// сплошное написание читается плохо: «Num 5» вместо «Num5».
QString keyLabelForVk(quint32 vk) {
    switch (vk) {
    case VK_MULTIPLY: return QStringLiteral("Num *");
    case VK_ADD:      return QStringLiteral("Num +");
    case VK_SUBTRACT: return QStringLiteral("Num -");
    case VK_DECIMAL:  return QStringLiteral("Num .");
    case VK_DIVIDE:   return QStringLiteral("Num /");
    case VK_SEPARATOR:return QStringLiteral("Num Enter");
    case VK_PRIOR:    return QStringLiteral("PgUp");
    case VK_NEXT:     return QStringLiteral("PgDn");
    case VK_INSERT:   return QStringLiteral("Ins");
    case VK_DELETE:   return QStringLiteral("Del");
    default: break;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return QStringLiteral("Num %1").arg(vk - VK_NUMPAD0);
    return keyTokenForVk(vk);
}

} // namespace

bool HotkeySpec::modsMatch(quint32 downSides) const {
    for (const ModClass& c : kClasses) {
        const quint32 sides = c.left | c.right;
        const quint32 have = downSides & sides;
        const quint32 want = mods & sides;
        if (want) {                       // сторона названа явно — только она
            if (have != want) return false;
        } else if (mods & c.any) {        // любая сторона, но хоть одна
            if (!have) return false;
        } else {                          // этот модификатор в бинде не участвует
            if (have) return false;
        }
    }
    return true;
}

quint32 HotkeySpec::anySide(quint32 mods) {
    quint32 out = 0;
    for (const ModClass& c : kClasses)
        if (mods & (c.any | c.left | c.right)) out |= c.any;
    return out;
}

quint32 HotkeySpec::modBitForVk(quint32 vk) {
    switch (vk) {
    case VK_LCONTROL: return LCtrl;
    case VK_RCONTROL: return RCtrl;
    case VK_LSHIFT:   return LShift;
    case VK_RSHIFT:   return RShift;
    case VK_LMENU:    return LAlt;
    case VK_RMENU:    return RAlt;
    case VK_LWIN:     return LWin;
    case VK_RWIN:     return RWin;
    default:          return 0;
    }
}

HotkeySpec HotkeySpec::parse(const QString& text) {
    HotkeySpec spec;
    if (text.trimmed().isEmpty()) return spec;

    bool keypad = false;                  // старый формат: "Num" отдельным токеном
    const QStringList tokens = text.split('+', Qt::SkipEmptyParts);
    for (const QString& raw : tokens) {
        const QString t = raw.trimmed();
        if (t.isEmpty()) continue;

        if (t.compare(QLatin1String("Num"), Qt::CaseInsensitive) == 0) { keypad = true; continue; }

        // Модификатор: имя класса с необязательной буквой стороны спереди.
        quint32 bit = 0;
        for (const ModClass& c : kClasses) {
            const QLatin1String name(c.name);
            if (t.compare(name, Qt::CaseInsensitive) == 0) { bit = c.any; break; }
            if (t.size() == name.size() + 1 &&
                t.mid(1).compare(name, Qt::CaseInsensitive) == 0) {
                const QChar side = t.at(0).toUpper();
                if (side == 'L') { bit = c.left;  break; }
                if (side == 'R') { bit = c.right; break; }
            }
        }
        // "Meta" — так Windows-клавиша называлась в тексте QKeySequence.
        if (!bit && t.compare(QLatin1String("Meta"), Qt::CaseInsensitive) == 0) bit = AnyWin;
        if (bit) { spec.mods |= bit; continue; }

        const quint32 vk = vkForKeyToken(t);
        if (!vk || spec.vk) return HotkeySpec();   // мусор или вторая обычная клавиша
        spec.vk = vk;
    }
    if (keypad && spec.vk) spec.vk = toKeypad(spec.vk);

    // Клавиша, которой в нашей таблице нет, и голый нумпад-префикс — не бинд.
    if (spec.vk && keyTokenForVk(spec.vk).isEmpty()) return HotkeySpec();
    return spec;
}

QString HotkeySpec::toText() const {
    if (isEmpty()) return QString();
    QStringList parts;
    for (const ModClass& c : kClasses) {
        if (mods & c.left)  parts << (QStringLiteral("L") + QLatin1String(c.name));
        if (mods & c.right) parts << (QStringLiteral("R") + QLatin1String(c.name));
        if (mods & c.any)   parts << QLatin1String(c.name);
    }
    if (vk) {
        const QString k = keyTokenForVk(vk);
        if (k.isEmpty()) return QString();
        parts << k;
    }
    return parts.join('+');
}

QString HotkeySpec::display(const QString& text) {
    const HotkeySpec spec = parse(text);
    if (spec.isEmpty()) return QString();

    QStringList parts;
    for (const ModClass& c : kClasses) {
        // Сторону показываем только там, где она есть в бинде: у сочетания
        // с обычной клавишей её нет, и «Пр.» там было бы неправдой.
        if (spec.mods & c.left)  parts << (QStringLiteral("Лев. ") + QLatin1String(c.name));
        if (spec.mods & c.right) parts << (QStringLiteral("Пр. ") + QLatin1String(c.name));
        if (spec.mods & c.any)   parts << QLatin1String(c.name);
    }
    if (spec.vk) parts << keyLabelForVk(spec.vk);
    return parts.join('+');
}
