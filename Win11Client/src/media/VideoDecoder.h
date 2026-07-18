#pragma once
#include <QByteArray>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

// Один видеодекодер FFmpeg (H.264 / VP8 / VP9 — по байту codec из заголовка
// кадра v2). Один экземпляр = один поток одного отправителя: у декодера
// внутреннее состояние (референсные кадры), смешивать потоки нельзя —
// то же правило, что у Opus-декодеров в AudioEngine.
class VideoDecoder {
public:
    ~VideoDecoder();

    bool open(quint8 protoCodec);   // Proto::CODEC_*; false — кодек не найден
    void close();

    // Скормить один чанк (= один закодированный кадр, так шлёт веб).
    // Возвращает декодированный кадр или nullptr, если кадра нет (это не
    // всегда ошибка). Владение кадром у декодера: указатель годен до
    // следующего decode(). После вызова проверяй failed(): true — декодер
    // сломан, чинить бесполезно, пересоздавай.
    const AVFrame* decode(const QByteArray& payload, quint64 tsMs);

    bool   isOpen() const { return m_ctx != nullptr; }
    bool   failed() const { return m_failed; }
    quint8 codec()  const { return m_codec; }

private:
    AVCodecContext* m_ctx = nullptr;
    AVFrame*  m_frame = nullptr;
    AVPacket* m_pkt   = nullptr;
    quint8 m_codec  = 0;
    bool   m_failed = false;
};
