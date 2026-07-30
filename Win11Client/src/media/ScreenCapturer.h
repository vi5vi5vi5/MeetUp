#pragma once
#include <QObject>
#include <QSize>
#include <QString>
#include <QVideoFrame>
#include <memory>

class QTimer;

// Захват экрана и окон через Windows.Graphics.Capture (этап B).
//
// Зачем свой захват вместо QScreenCapture. Тот тикает по таймеру и на каждый
// тик тащит кадр из видеопамяти в обычную, менялось что-нибудь или нет: на
// замере он отдавал 27 к/с на мониторе 60 Гц, из них 61 % кадров были
// байт-в-байт предыдущими. Ни частотой, ни выбором «что считать изменением»
// оттуда управлять нельзя — публичного API нет. WGC даёт и то и другое, плюс
// захват ОТДЕЛЬНОГО ОКНА (Desktop Duplication этого не умеет в принципе) и
// курсор одним флагом.
//
// Зависимостей не добавляет: WGC живёт в Windows SDK, линкуется windowsapp.lib.
//
// Поток. Кадры приезжают от системы на её собственном потоке пула, поэтому
// frameReady приходит НЕ на том потоке, где создавали объект — подключаться
// нужно queued. Сам объект надо создавать на потоке с апартаментом COM = MTA
// и с работающим циклом событий (для сторожа свёрнутого окна); поток
// кодирования в VideoEngine ровно такой.
class ScreenCapturer : public QObject {
    Q_OBJECT
public:
    explicit ScreenCapturer(QObject* parent = nullptr);
    ~ScreenCapturer() override;

    // Есть ли WGC в этой системе. Win10 1903+; переключение курсора — 2004+,
    // снятие жёлтой рамки — Win11. Целевая система проекта — Win11.
    static bool isSupported();

    // Источник задаётся хендлом, а не абстракцией Qt: свой выбор экрана и окна
    // у нас уже есть (ScreenSources), а системный пикер Windows тут не нужен.
    // hmonitor/hwnd — HMONITOR/HWND; void*, чтобы не тащить windows.h в заголовок.
    bool startMonitor(void* hmonitor, bool drawCursor);
    bool startWindow(void* hwnd, bool drawCursor);
    void stop();

    bool isRunning() const;
    // Курсор рисует система — на лету, без перезапуска захвата.
    void setCursorEnabled(bool on);

signals:
    // Готовый кадр BGRA в системной памяти. Приезжает с потока пула WGC.
    void frameReady(const QVideoFrame& frame);
    // Источник исчез: окно закрыли, монитор отключили, захват не поднялся.
    void failed(const QString& text);
    // Окно свернули — система перестаёт отдавать кадры, а нам надо отличить
    // это от «на экране просто ничего не происходит». true — кадров не будет,
    // false — снова пошли.
    void suspendedChanged(bool suspended);

public:
    // Публичная только формально: тип непрозрачный, определён в .cpp и держит
    // объекты WinRT. Наружу вынесен затем, что с ним работают свободные функции
    // в .cpp — тащить winrt в этот заголовок ради них было бы хуже.
    // (И объявлен ПОСЛЕ явного public: — иначе moc считает его сигналом.)
    struct Impl;

private:
    std::unique_ptr<Impl> d;

    void onWatchdog();
    bool startItem(bool drawCursor, const QString& what);

    QTimer* m_watchdog = nullptr;
};
