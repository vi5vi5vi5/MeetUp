#pragma once
#include <QByteArray>
#include <QList>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

// Видеокодер отправки (M4): один поток камеры -> H.264 Annex B (libopenh264)
// или VP8 (libvpx) — первый доступный, в порядке предпочтения веба. Веб-клиент
// декодирует оба; наш приёмник — тоже (FFmpeg). Реалтайм-профиль: без B-кадров,
// keyframe'ы только по требованию снаружи (каждые ~72 кадра или KEYFRAME_REQ).
class VideoEncoder {
public:
    ~VideoEncoder();

    // Открыть кодер под размер кадра (размеры чётные — правило §5.5).
    // false — ни одного подходящего энкодера в сборке FFmpeg нет.
    bool open(int width, int height, int fps, int bitrate);
    void close();

    bool isOpen() const { return m_ctx != nullptr; }
    quint8 protoCodec() const { return m_protoCodec; }   // Proto::CODEC_H264 | CODEC_VP8
    int width() const { return m_width; }
    int height() const { return m_height; }

    // Кадр для заполнения (YUV420P, m_width x m_height): пиши в него через
    // sws_scale и отдавай в encode(). Между вызовами кадр переиспользуется.
    AVFrame* frame();

    struct Packet {
        QByteArray data;
        bool key = false;
    };
    // Закодировать текущий frame(). ptsMs — внутренняя монотонная шкала (мс).
    // Возвращает 0..n пакетов (реалтайм-кодеры почти всегда отдают 1 сразу).
    QList<Packet> encode(bool keyframe, qint64 ptsMs);

private:
    bool tryOpen(const char* encoderName, quint8 protoCodec,
                 int width, int height, int fps, int bitrate);

    AVCodecContext* m_ctx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_pkt = nullptr;
    quint8 m_protoCodec = 0;
    int m_width = 0, m_height = 0;
};
