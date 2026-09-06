#pragma once
#include <QObject>
#include <QSet>
#include <QElapsedTimer>
#include "HotkeySpec.h"

class MediaSettings;

// Глобальные горячие клавиши (M8). Бинды нужны именно тогда, когда окно не в
// фокусе: человек демонстрирует экран из другой программы и хочет выключить
// микрофон, не возвращаясь к нам. QML-Shortcut так не умеет.
//
// Слушаем СЫРОЙ ВВОД клавиатуры (Raw Input с RIDEV_INPUTSINK), а не
// RegisterHotKey, как было раньше. Разница принципиальная, ради неё всё и
// переделано:
//
//  • RegisterHotKey ЗАНИМАЕТ сочетание монопольно на всю систему — второй
//    запущенный клиент получал отказ, и клавиши в нём молчали;
//  • он же ИЗЫМАЕТ клавишу из ввода — бинд на букве означал, что эту букву
//    больше нигде не напечатать;
//  • и он не выражает ни одинокого модификатора, ни стороны клавиши — «правый
//    Ctrl» на нём не назначался в принципе.
//
// Сырой ввод не занимает ничего и никого клавиш не лишает: система отдаёт нам
// копию событий, а клавиша идёт дальше своей дорогой. Поэтому два наших
// клиента слышат нажатие одинаково (так и задумано), а бинд на букве не мешает
// эту букву печатать.
//
// Бинд ИЗ ОДНИХ МОДИФИКАТОРОВ срабатывает на отпускании и только если аккорд
// был чистым — между нажатием и отпусканием не трогали других клавиш. Иначе
// «правый Ctrl» на микрофоне срабатывал бы на каждом RCtrl+C, а отличить его
// от «правый Ctrl + правый Shift» было бы нечем: на отпускании же видно, до
// чего аккорд дорос. Бинд с обычной клавишей срабатывает как везде — сразу на
// нажатии.
//
// РАЦИЯ — исключение из всего абзаца выше. Там важно не «сработало», а
// «зажато», поэтому клавиша микрофона в этом режиме не даёт micHotkey вовсе:
// вместо него идёт pttChanged(true/false) по фактическому состоянию клавиш.
// Аккорд из одних модификаторов в рации срабатывает сразу на нажатии — ждать
// отпускания, чтобы открыть микрофон, было бы бессмысленно.
class GlobalHotkeys : public QObject {
    Q_OBJECT
public:
    // Порядок задаёт и порядок в m_specs, и порядок разбора в fire().
    enum Action { Mic = 0, Sound, Cam, Share, Full, Leave, ActionCount };

    explicit GlobalHotkeys(MediaSettings* settings, QObject* parent = nullptr);
    ~GlobalHotkeys() override;

    // Слушаем клавиши, только пока активны: конференция открыта и не идёт
    // назначение клавиши (иначе бинд перехватывал бы сам себя при захвате).
    Q_INVOKABLE void setActive(bool active);

    // Назначение клавиши в настройках. Событий Qt для этого не хватает —
    // одинокий модификатор со стороной в них не выражается, — поэтому аккорд
    // собирает тот же приёмник сырого ввода и отдаёт готовый текст сигналом
    // captured().
    Q_INVOKABLE void startCapture();
    Q_INVOKABLE void stopCapture();

    // Человекочитаемый вид бинда: "RCtrl+RShift" -> "Пр. Ctrl+Пр. Shift".
    Q_INVOKABLE QString label(const QString& text) const;

    // Зовётся оконной процедурой приёмника; снаружи не нужен.
    void handleRawInput(void* rawInputHandle);

signals:
    void micHotkey();
    void soundHotkey();
    void camHotkey();
    void shareHotkey();
    void fullScreenHotkey();
    void leaveHotkey();
    // Рация: клавиша микрофона зажата (true) или отпущена (false).
    void pttChanged(bool down);
    void captured(const QString& text);

private:
    void reloadBindings();          // перечитать бинды из настроек
    void refreshListener();         // подписка на сырой ввод нужна или нет
    bool startListening();
    void stopListening();
    void resetChord();              // забыть, что было зажато

    void onKey(quint32 vk, bool down);
    void endChord();                // отпущен последний модификатор аккорда
    void fire(const HotkeySpec& pressed);
    void finishCapture(const HotkeySpec& spec);
    // Рация: зажат ли ПРЯМО СЕЙЧАС бинд микрофона. Считается по состоянию
    // клавиш, а не по событиям, — так микрофон не может залипнуть открытым из-за
    // потерянного отпускания (например, если клавишу отпустили, пока мы не
    // слушали ввод).
    bool pttHeld() const;
    void syncPtt();                 // сверить состояние клавиш с сигналом
    // Окно приложения в фокусе и человек печатает — бинд с обычной клавишей
    // тогда не наш: клавиша должна дойти до поля ввода и только туда.
    bool typingInOurWindow() const;

    MediaSettings* m_settings;      // не владеем
    HotkeySpec m_specs[ActionCount];
    bool m_active = false;
    bool m_capture = false;
    bool m_ptt = false;             // режим рации включён в настройках
    bool m_pttDown = false;         // …и клавиша сейчас зажата

    void* m_hwnd = nullptr;         // окно-приёмник сырого ввода (HWND)
    bool m_listening = false;

    quint32 m_modsDown = 0;         // модификаторы, зажатые сейчас (биты сторон)
    quint32 m_chordPeak = 0;        // сколько их было зажато одновременно
    bool m_chordDirty = false;      // в аккорде побывала обычная клавиша
    QSet<quint32> m_keysDown;       // обычные клавиши — против автоповтора

    // Раскладки с AltGr шлют перед правым Alt фальшивый Ctrl. Отличить его от
    // настоящего можно только по времени: он приходит тем же пакетом ввода.
    QElapsedTimer m_clock;
    qint64 m_ctrlDownAt = 0;
    quint32 m_ctrlDownBit = 0;
};
