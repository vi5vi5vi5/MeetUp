#pragma once
#include <QString>
#include <QtGlobal>

// Горячая клавиша в разобранном виде: набор модификаторов и, возможно, обычная
// клавиша.
//
// QKeySequence на эту роль не годится принципиально. Он не умеет ни одинокого
// модификатора («правый Ctrl» сам по себе, без сочетания), ни стороны клавиши
// (правый Ctrl отдельно от левого) — а удобны для бинда как раз такие клавиши,
// которые больше ни во что не вмешиваются. Поэтому свой формат.
//
// Текст бинда — токены через «+», модификаторы первыми, в порядке
// Ctrl, Shift, Alt, Win:
//     "RCtrl"          — правый Ctrl, одиночным нажатием
//     "RCtrl+RShift"   — правые Ctrl и Shift вместе
//     "Ctrl+D"         — обычное сочетание, Ctrl любой
//     "F9", "Num5"     — одна клавиша
//
// Сторона значима ТОЛЬКО у бинда из одних модификаторов — в нём весь смысл.
// В сочетании с обычной клавишей она не запоминается: Ctrl+D привычно работает
// любым Ctrl, и обманывать это ожидание незачем. Приводит текст к такому виду
// сам захват (GlobalHotkeys::finishCapture).
struct HotkeySpec {
    enum Mod : quint32 {
        LCtrl  = 1u << 0,  RCtrl  = 1u << 1,
        LShift = 1u << 2,  RShift = 1u << 3,
        LAlt   = 1u << 4,  RAlt   = 1u << 5,
        LWin   = 1u << 6,  RWin   = 1u << 7,
        // «Любая сторона» — для сочетаний с обычной клавишей.
        AnyCtrl = 1u << 8, AnyShift = 1u << 9, AnyAlt = 1u << 10, AnyWin = 1u << 11,
    };
    static constexpr quint32 SideBits = 0xFFu;   // маска битов L*/R*

    quint32 mods = 0;   // набор Mod
    quint32 vk = 0;     // виртуальный код обычной клавиши; 0 — аккорд из одних модификаторов

    bool isEmpty() const { return mods == 0 && vk == 0; }
    bool modifierOnly() const { return vk == 0 && mods != 0; }

    // Подходит ли набор зажатых сейчас модификаторов (только биты L*/R*).
    // Проверка строгая в обе стороны: недостающий модификатор — не наш случай,
    // но и лишний тоже, иначе Ctrl+Shift+D срабатывал бы как Ctrl+D.
    bool modsMatch(quint32 downSides) const;

    // Текст -> разбор. Понимает и старый формат QKeySequence ("Ctrl+D",
    // "Num+5", "Meta+K", "PgUp") — бинды, сохранённые прежней версией,
    // переживают обновление.
    static HotkeySpec parse(const QString& text);
    QString toText() const;                                     // канонический текст

    // Привести текст к каноническому виду; пусто — если разобрать не вышло.
    static QString normalize(const QString& text) { return parse(text).toText(); }
    // Для интерфейса: "RCtrl+RShift" -> "Пр. Ctrl+Пр. Shift".
    static QString display(const QString& text);

    // Стороны -> классы: LCtrl|RAlt -> AnyCtrl|AnyAlt. Биты «любой стороны»
    // проходят насквозь.
    static quint32 anySide(quint32 mods);
    // Виртуальный код модификатора -> его бит (0, если клавиша обычная).
    static quint32 modBitForVk(quint32 vk);
};
