#pragma once
#include <QObject>
#include <QByteArray>
#include <QVideoFrame>

class VideoEncoder;
struct SwsContext;

// Кодирование видео отправки живёт на ОТДЕЛЬНОМ потоке. sws_scale + H.264/VP8 —
// это единицы-десятки миллисекунд на кадр; на GUI-потоке они запинали бы
// отрисовку — при перетаскивании окна (модальный цикл перемещения у Windows)
// кадр энкодера блокировал бы обработку сообщений move, и окно дёргалось бы.
// Кадр камеры прилетает сюда queued-сигналом, наружу уходят готовые пакеты v2.
class VideoSendWorker : public QObject {
    Q_OBJECT
public:
    // msgType — тип кадра в заголовке v2: VIDEO_CODED (камера) или
    // SCREEN_CODED (демонстрация экрана). Всё остальное у полос одинаково,
    // поэтому воркеров просто два — со своими энкодерами и своей каденцией.
    explicit VideoSendWorker(quint8 msgType, QObject* parent = nullptr);
    ~VideoSendWorker() override;

public slots:
    // Один кадр камеры -> пакеты. maxW/maxH/fps/bitrate — пресет качества;
    // forceKey — опорный кадр по требованию (KEYFRAME_REQ / новичок / реконнект),
    // каденс раз в 72 кадра worker ведёт сам. tsMs — Date.now() отправителя
    // (уходит в заголовок кадра, §5.3).
    void encode(const QVideoFrame& frame, int maxW, int maxH, int fps,
                int bitrate, bool forceKey, qint64 tsMs);
    // Захват остановлен / сменились параметры: энкодер в мусор, следующий
    // запуск начнётся с опорного кадра.
    void reset();

signals:
    // Готовый пакет v2 (уже упакован Proto::pack) — GUI-поток отправит в сокет.
    void packetReady(const QByteArray& frame);

private:
    quint8 m_msgType;
    VideoEncoder* m_enc = nullptr;
    SwsContext* m_sws = nullptr;
    int m_frames = 0;
    bool m_keyNext = true;
    qint64 m_sendStartMs = 0;      // база внутренней PTS-шкалы кодера
};
