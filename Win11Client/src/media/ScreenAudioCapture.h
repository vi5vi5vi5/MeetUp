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
// Ключевая деталь — режим PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE
// со своим PID: захватывается всё, что играет система, КРОМЕ нас самих.
// Без этого в демонстрацию попали бы голоса собеседников из наших же
// динамиков, и они услышали бы себя с задержкой. Исключение проверено пробой
// (scratchpad/looptest): свой тон оказался в 450 раз тише, чем при включении.
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
};
