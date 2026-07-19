#include "VideoEncoder.h"
#include "../net/Protocol.h"
#include <QDebug>
#include <QThread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

VideoEncoder::~VideoEncoder() { close(); }

bool VideoEncoder::tryOpen(const char* encoderName, quint8 protoCodec,
                           int width, int height, int fps, int bitrate) {
    const AVCodec* codec = avcodec_find_encoder_by_name(encoderName);
    if (!codec) return false;

    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx) return false;

    m_ctx->width = width;
    m_ctx->height = height;
    m_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    m_ctx->time_base = { 1, 1000 };          // PTS в миллисекундах
    m_ctx->framerate = { fps, 1 };
    m_ctx->bit_rate = bitrate;
    // Keyframe-каденс ведём сами (каждые ~72 кадра + по KEYFRAME_REQ):
    // кодеру запрещаем самодеятельность огромным GOP, опорные кадры форсируем
    // через pict_type. B-кадров нет — конференция, задержка важнее сжатия.
    m_ctx->gop_size = 30000;
    m_ctx->max_b_frames = 0;
    m_ctx->thread_count = qBound(1, QThread::idealThreadCount() / 2, 4);
    // global_header НЕ ставим: SPS/PPS должны повторяться в каждом keyframe
    // (веб-декодер настроен на Annex B без отдельной extradata — §6.1).

    AVDictionary* opts = nullptr;
    if (protoCodec == Proto::CODEC_H264) {
        // openh264: реалтайм по своей природе. Baseline — как avc1.42E01F у веба.
        av_dict_set(&opts, "profile", "constrained_baseline", 0);
        av_dict_set(&opts, "rc_mode", "bitrate", 0);
        av_dict_set(&opts, "allow_skip_frames", "0", 0);
    } else {
        // libvpx: без явного deadline=realtime кодирует сотни мс на кадр.
        av_dict_set(&opts, "deadline", "realtime", 0);
        av_dict_set(&opts, "cpu-used", "8", 0);
        av_dict_set(&opts, "lag-in-frames", "0", 0);
        av_dict_set(&opts, "error-resilient", "1", 0);
    }

    const int rc = avcodec_open2(m_ctx, codec, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        avcodec_free_context(&m_ctx);
        return false;
    }

    m_frame = av_frame_alloc();
    m_pkt = av_packet_alloc();
    if (!m_frame || !m_pkt) { close(); return false; }
    m_frame->format = AV_PIX_FMT_YUV420P;
    m_frame->width = width;
    m_frame->height = height;
    if (av_frame_get_buffer(m_frame, 0) < 0) { close(); return false; }

    m_protoCodec = protoCodec;
    m_width = width;
    m_height = height;
    qInfo() << "VideoEncoder:" << encoderName << width << "x" << height
            << fps << "fps" << bitrate << "bps";
    return true;
}

bool VideoEncoder::open(int width, int height, int fps, int bitrate) {
    close();
    width &= ~1;                             // чётные размеры — правило §5.5
    height &= ~1;
    if (width < 2 || height < 2) return false;

    // Порядок предпочтения — как у веба: H.264 (аппаратный декод почти у всех
    // приёмников), затем VP8. Оба уходят одним и тем же протоколом v2.
    if (tryOpen("libopenh264", Proto::CODEC_H264, width, height, fps, bitrate)) return true;
    if (tryOpen("libvpx", Proto::CODEC_VP8, width, height, fps, bitrate)) return true;
    qWarning() << "VideoEncoder: в сборке FFmpeg нет ни libopenh264, ни libvpx";
    return false;
}

void VideoEncoder::close() {
    if (m_pkt) av_packet_free(&m_pkt);
    if (m_frame) av_frame_free(&m_frame);
    if (m_ctx) avcodec_free_context(&m_ctx);
    m_protoCodec = 0;
    m_width = m_height = 0;
}

AVFrame* VideoEncoder::frame() {
    if (!m_frame) return nullptr;
    // Кадр мог остаться «в аренде» у кодера с прошлого encode() — вернуть себе.
    if (av_frame_make_writable(m_frame) < 0) return nullptr;
    return m_frame;
}

QList<VideoEncoder::Packet> VideoEncoder::encode(bool keyframe, qint64 ptsMs) {
    QList<Packet> out;
    if (!m_ctx || !m_frame) return out;

    m_frame->pts = ptsMs;
    // Форсированный опорный кадр: оба врапера (libopenh264, libvpx) понимают
    // pict_type = I как команду «этот кадр — keyframe».
    m_frame->pict_type = keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;

    if (avcodec_send_frame(m_ctx, m_frame) < 0) return out;
    while (avcodec_receive_packet(m_ctx, m_pkt) == 0) {
        Packet p;
        p.data = QByteArray(reinterpret_cast<const char*>(m_pkt->data), m_pkt->size);
        p.key = (m_pkt->flags & AV_PKT_FLAG_KEY) != 0;
        out.append(p);
        av_packet_unref(m_pkt);
    }
    return out;
}
