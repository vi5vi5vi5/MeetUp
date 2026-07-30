#pragma once
#include <QByteArray>
#include <QList>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

// Видеокодер отправки (M4): один поток кадров -> H.264 Annex B или VP8/VP9 —
// первый доступный, в порядке предпочтения веба. Веб-клиент декодирует всё это;
// наш приёмник — тоже (FFmpeg). Реалтайм-профиль: без B-кадров, keyframe'ы
// только по требованию снаружи (каждые ~72 кадра или KEYFRAME_REQ).
//
// H.264 бывает двух видов, и это не деталь реализации, а разные конвейеры:
// «h264_mf» — аппаратный кодировщик видеокарты через Media Foundation, «-mf»
// требует на входе NV12 и буферизует кадры; «libopenh264» — программный, берёт
// YUV420P и отдаёт пакет сразу. Наружу оба — Proto::CODEC_H264: приёмник
// разницы не видит. Поэтому формат входного кадра спрашивают у нас (pixFmt), а
// не считают известным заранее.
class VideoEncoder {
public:
    ~VideoEncoder();

    // Открыть кодер под размер кадра (размеры чётные — правило §5.5).
    // preferred — Proto::CODEC_* по выбору пользователя, 0 = как раньше (H.264,
    // при неудаче VP8). Выбранный кодек пробуется первым, но остальные остаются
    // запасными: остаться вовсе без картинки хуже, чем вещать не тем кодеком.
    // allowHardware — пускать ли вперёд аппаратный H.264. Его включает только
    // полоса демонстрации: см. комментарий в VideoSendWorker.
    // false — ни одного подходящего энкодера в сборке FFmpeg нет.
    bool open(int width, int height, int fps, int bitrate, quint8 preferred = 0,
              bool allowHardware = false);
    void close();

    // Доступен ли АППАРАТНЫЙ HEVC на этой машине. Спрашивается один раз и
    // запоминается: ответ не меняется, а стоит он создания MFT.
    //
    // Проверяем именно открытие с hw_encoding=1, а не наличие «hevc_mf» в
    // сборке: имя есть всегда, а железо — нет. Программный HEVC нам не нужен
    // ни при каких условиях (он медленнее openh264 и всё равно патентованный),
    // поэтому «нет железа» = «пункта нет».
    //
    // ЗВАТЬ ТОЛЬКО С ПОТОКА, ГДЕ АПАРТАМЕНТ COM = MTA. На GUI-потоке Qt (он
    // всегда STA) Media Foundation откажет, и мы получили бы ложное «нет».
    static bool hardwareHevcAvailable();
    // То же для AV1. Он безроялтийный, поэтому единственный, кого не жалко
    // однажды поставить по умолчанию, — но только когда его научатся
    // принимать все, включая веб.
    static bool hardwareAv1Available();

    bool isOpen() const { return m_ctx != nullptr; }
    quint8 protoCodec() const { return m_protoCodec; }   // Proto::CODEC_H264 | VP8 | VP9
    int width() const { return m_width; }
    int height() const { return m_height; }
    // Формат кадра, который ждёт открытый кодировщик: AV_PIX_FMT_YUV420P или
    // AV_PIX_FMT_NV12 (int, чтобы не тащить сюда заголовки FFmpeg).
    int pixFmt() const { return m_pixFmt; }
    // Аппаратный ли. Нужно наружу ровно затем, чтобы сказать это человеку в
    // «Диагностике»: 30 к/с на видеокарте и 30 к/с на процессоре — это разная
    // осведомлённость о том, чего ждать от машины.
    bool isHardware() const { return m_hardware; }

    AVFrame* frame();

    struct Packet {
        QByteArray data;
        bool key = false;
        // Метка кадра, из которого получился ЭТОТ пакет. У аппаратного
        // кодировщика конвейер глубиной два кадра, и пакет, вышедший из
        // encode(), относится не к тому кадру, который туда сейчас положили.
        // Без этой метки в заголовок уходило бы время, на два кадра новее
        // картинки, — а по нему приёмник синхронизирует губы.
        qint64 ptsMs = 0;
    };
    // Закодировать текущий frame(). ptsMs — внутренняя монотонная шкала (мс).
    // Возвращает 0..n пакетов: программные кодеры почти всегда отдают 1 сразу,
    // аппаратный — первые два раза ни одного.
    QList<Packet> encode(bool keyframe, qint64 ptsMs);

private:
    struct Candidate {
        const char* name;
        quint8 proto;
        int pixFmt;              // AVPixelFormat
        bool hardware;
    };
    bool tryOpen(const Candidate& cand, int width, int height, int fps, int bitrate);

    AVCodecContext* m_ctx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_pkt = nullptr;
    quint8 m_protoCodec = 0;
    int m_width = 0, m_height = 0;
    int m_pixFmt = 0;            // AV_PIX_FMT_YUV420P
    bool m_hardware = false;
};
