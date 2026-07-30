#include "VideoDecoder.h"
#include "../net/Protocol.h"
#include <QDebug>

#include <QMutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

// Байт codec протокола → идентификатор кодека FFmpeg.
static AVCodecID toAvCodec(quint8 protoCodec) {
    switch (protoCodec) {
    case Proto::CODEC_H264: return AV_CODEC_ID_H264;
    case Proto::CODEC_VP8:  return AV_CODEC_ID_VP8;
    case Proto::CODEC_VP9:  return AV_CODEC_ID_VP9;
    // Принимать HEVC умеем всегда, даже если отправлять не можем: декодер
    // безусловно есть в сборке FFmpeg, а на видеокартах с поддержкой его
    // подхватит аппаратный путь. Возможность ПРИНЯТЬ шире возможности послать —
    // так и должно быть, иначе выбор кодека у собеседника ломал бы нам картинку.
    case Proto::CODEC_HEVC: return AV_CODEC_ID_HEVC;
    case Proto::CODEC_AV1:  return AV_CODEC_ID_AV1;
    default:                return AV_CODEC_ID_NONE;
    }
}

VideoDecoder::~VideoDecoder() { close(); }

// Одно устройство D3D11 на всё приложение. Декодеров бывает много (по одному
// на отправителя в каждой полосе), а устройство им нужно одно и то же —
// заводить по штуке на каждого значило бы держать десяток лишних.
static AVBufferRef* sharedD3d11Device() {
    static QMutex lock;
    static AVBufferRef* dev = nullptr;
    static bool tried = false;
    QMutexLocker guard(&lock);
    if (!tried) {
        tried = true;
        if (av_hwdevice_ctx_create(&dev, AV_HWDEVICE_TYPE_D3D11VA,
                                   nullptr, nullptr, 0) < 0) {
            dev = nullptr;
            qWarning() << "VideoDecoder: D3D11 для декодирования недоступен,"
                          " разбираем процессором";
        }
    }
    return dev;
}

// Какой формат кадра выбрать. Просим текстуру D3D11; если её в списке нет —
// значит для этого потока видеокарта не годится (не тот профиль, не тот
// кодек), и FFmpeg предлагает только программные форматы. Берём первый — это
// и есть штатный откат, никакой ошибки тут нет.
static AVPixelFormat pickFormat(AVCodecContext*, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == AV_PIX_FMT_D3D11) return *p;
    return fmts[0];
}

// Умеет ли этот декодер работать через D3D11VA.
static bool supportsD3d11(const AVCodec* codec) {
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
        if (!cfg) return false;
        if (cfg->device_type == AV_HWDEVICE_TYPE_D3D11VA
            && (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
            return true;
    }
}

bool VideoDecoder::open(quint8 protoCodec) {
    close();

    const AVCodec* codec = avcodec_find_decoder(toAvCodec(protoCodec));
    if (!codec) {
        qWarning() << "VideoDecoder: нет декодера для кодека" << protoCodec;
        return false;
    }

    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx) return false;

    // Конференция = минимальная задержка (веб задаёт optimizeForLatency).
    // Наш перевод на язык FFmpeg: один поток декодирования (многопоточный
    // режим буферизует кадры — плюс задержка) и явный low delay.
    //
    // Многопоточность мы сознательно НЕ включаем даже на тяжёлых потоках.
    // Замер: у HEVC 4K она снижает среднее вдвое (13.1 -> 6.4 мс), но не
    // трогает выбросы (108 мс так и остаются) и стоит трёх кадров задержки.
    // Рвёт картинку именно выброс, а не среднее, — значит платить задержкой
    // не за что. Настоящее решение ниже: отдать разбор видеокарте.
    m_ctx->thread_count = 1;
    m_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    // Разбор на видеокарте. Тот же блок кремния, что кодирует, и он снимает
    // и среднее, и выбросы: у HEVC 4K программный разбор стоил 13 мс среднего
    // при 108 худших — при бюджете 16 мс на кадр это гарантированный обвал.
    // Не получилось — молча работаем как раньше: остаться без картинки хуже,
    // чем разобрать её медленно.
    if (AVBufferRef* dev = supportsD3d11(codec) ? sharedD3d11Device() : nullptr) {
        m_ctx->hw_device_ctx = av_buffer_ref(dev);
        m_ctx->get_format = pickFormat;
    }

    if (avcodec_open2(m_ctx, codec, nullptr) < 0) { close(); return false; }

    m_frame = av_frame_alloc();
    m_ready = av_frame_alloc();
    m_sw    = av_frame_alloc();
    m_pkt   = av_packet_alloc();
    if (!m_frame || !m_ready || !m_sw || !m_pkt) { close(); return false; }

    m_codec  = protoCodec;
    m_failed = false;
    return true;
}

void VideoDecoder::close() {
    if (m_pkt)   av_packet_free(&m_pkt);      // сами обнуляют указатель
    if (m_sw)    av_frame_free(&m_sw);
    if (m_ready) av_frame_free(&m_ready);
    if (m_frame) av_frame_free(&m_frame);
    if (m_ctx)   avcodec_free_context(&m_ctx);   // отпустит и ссылку на устройство
    m_codec  = 0;
    m_failed = false;
    m_hardware = false;
}

const AVFrame* VideoDecoder::decode(const QByteArray& payload, quint64 tsMs) {
    if (!m_ctx) return nullptr;

    // Пакет указывает на чужие байты (payload) без владения — send_packet
    // скопирует внутрь всё, что ему нужно пережить.
    m_pkt->data = reinterpret_cast<uint8_t*>(const_cast<char*>(payload.constData()));
    m_pkt->size = int(payload.size());
    // PTS: мс эпохи отправителя. Декодеру важна лишь монотонность — а она
    // в рамках одного отправителя гарантирована самой природой Date.now().
    m_pkt->pts = qint64(tsMs);

    const int sent = avcodec_send_packet(m_ctx, m_pkt);
    m_pkt->data = nullptr;
    m_pkt->size = 0;
    if (sent < 0) { m_failed = true; return nullptr; }

    // Дренаж: забираем ВСЕ готовые кадры и отдаём последний. Один кадр,
    // застрявший внутри декодера, — это постоянные лишние ~40 мс задержки,
    // и они уже не рассасываются: отставание живёт до конца потока.
    //
    // Грабля: avcodec_receive_frame ОЧИЩАЕТ переданный кадр даже когда
    // возвращает EAGAIN. Поэтому каждый удачный кадр немедленно перекладываем
    // в отдельный m_ready — иначе наружу уходил бы уже стёртый кадр 0x0.
    bool haveFrame = false;
    for (;;) {
        const int got = avcodec_receive_frame(m_ctx, m_frame);
        if (got == AVERROR(EAGAIN) || got == AVERROR_EOF)
            break;                             // кадр ещё «зреет» — не ошибка
        if (got < 0) { m_failed = true; return nullptr; }
        av_frame_unref(m_ready);
        av_frame_move_ref(m_ready, m_frame);
        haveFrame = true;
    }

    if (!haveFrame) return nullptr;

    // Разобрала видеокарта — кадр лежит текстурой в видеопамяти, а рисовать
    // его нам через QVideoSink, то есть нужна обычная память. Перенос отдаёт
    // NV12; дальше по пути (VideoRecvWorker::convert) он уходит в QVideoFrame
    // без единой конверсии — формат совпадает.
    if (m_ready->format == AV_PIX_FMT_D3D11) {
        m_hardware = true;
        av_frame_unref(m_sw);
        if (av_hwframe_transfer_data(m_sw, m_ready, 0) < 0) {
            m_failed = true;
            return nullptr;
        }
        av_frame_copy_props(m_sw, m_ready);
        return m_sw;
    }

    m_hardware = false;
    return m_ready;
}
