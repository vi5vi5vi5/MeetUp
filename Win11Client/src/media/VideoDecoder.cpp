#include "VideoDecoder.h"
#include "../net/Protocol.h"
#include <QDebug>

extern "C" {
#include <libavcodec/avcodec.h>
}

// Байт codec протокола → идентификатор кодека FFmpeg.
static AVCodecID toAvCodec(quint8 protoCodec) {
    switch (protoCodec) {
    case Proto::CODEC_H264: return AV_CODEC_ID_H264;
    case Proto::CODEC_VP8:  return AV_CODEC_ID_VP8;
    case Proto::CODEC_VP9:  return AV_CODEC_ID_VP9;
    default:                return AV_CODEC_ID_NONE;
    }
}

VideoDecoder::~VideoDecoder() { close(); }

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
    m_ctx->thread_count = 1;
    m_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    if (avcodec_open2(m_ctx, codec, nullptr) < 0) { close(); return false; }

    m_frame = av_frame_alloc();
    m_ready = av_frame_alloc();
    m_pkt   = av_packet_alloc();
    if (!m_frame || !m_ready || !m_pkt) { close(); return false; }

    m_codec  = protoCodec;
    m_failed = false;
    return true;
}

void VideoDecoder::close() {
    if (m_pkt)   av_packet_free(&m_pkt);      // сами обнуляют указатель
    if (m_ready) av_frame_free(&m_ready);
    if (m_frame) av_frame_free(&m_frame);
    if (m_ctx)   avcodec_free_context(&m_ctx);
    m_codec  = 0;
    m_failed = false;
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

    return haveFrame ? m_ready : nullptr;
}
