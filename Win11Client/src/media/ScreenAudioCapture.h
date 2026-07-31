#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <atomic>
#include <thread>

// Захват звука ПРИЛОЖЕНИЙ для демонстрации экрана (WASAPI process loopback,
// Windows 10 2004+). Отдаёт готовые кадры 20 мс: 48 кГц, моно, Int16 —
// ровно то, что ест Opus в AudioEngine.
//
// У захвата два режима, и выбирает между ними то, ЧТО демонстрируется.
//
//   Монитор. PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE со своим PID:
//   всё, что играет система, КРОМЕ нас самих. Без исключения в демонстрацию
//   попали бы голоса собеседников из наших же динамиков, и они услышали бы
//   себя с задержкой. Проверено пробой (scratchpad/looptest): свой тон вышел
//   в 450 раз тише, чем при включении.
//
//   Отдельное окно. PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE с PID
//   этого окна: только его звук. Показывая один документ, не делимся заодно
//   музыкой из плеера, а собеседников исключать уже не нужно — мы их и не
//   включали.
//
// Ограничение, которое не обойти: Windows разделяет звук по ПРОЦЕССАМ, а не по
// окнам. Одно окно браузера с десятком вкладок — один процесс (точнее, одно
// дерево), и звук придёт от всех вкладок сразу. Слово TREE в названии режима
// здесь работает на нас: браузеры выносят звук в отдельный служебный процесс,
// и он дочерний, то есть в дерево попадает.
//
// Обычный «петлевой» захват устройства (AUDCLNT_STREAMFLAGS_LOOPBACK на
// endpoint) так не умеет — он отдаёт общий микс вместе с нашим звуком, и
// формат навязывает своё (mix format), который пришлось бы ресемплировать.
// Process loopback позволяет задать формат самим — поэтому и он.
class ScreenAudioCapture : public QObject {
    Q_OBJECT
public:
    explicit ScreenAudioCapture(QObject* parent = nullptr);
    ~ScreenAudioCapture() override;

    // Чей звук снимать: 0 — всё, кроме нас (показываем монитор), иначе —
    // только дерево процессов с этим PID (показываем его окно).
    //
    // Цель читается в момент активации клиента WASAPI и на лету не меняется,
    // поэтому смена во время захвата означает перезапуск. Вызванный до start()
    // просто запоминает — перезапускать нечего.
    void setTarget(quint32 pid);

    void start();
    void stop();
    bool active() const { return m_running.load(); }

signals:
    // Кадр 960 сэмплов Int16 моно. wallMs — стенные часы момента захвата;
    // по ним владелец подтягивает свои монотонные метки, если разошлись.
    void pcmFrame(const QByteArray& pcm, qint64 wallMs);
    // Захват не поднялся или оборвался: текст уже человеческий, для тоста.
    void failed(const QString& text);

private:
    void run();                       // тело потока захвата

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
    std::atomic<quint32> m_targetPid{0};   // 0 — «всё, кроме нас»; см. setTarget
};
