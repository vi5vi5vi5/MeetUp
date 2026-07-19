#include "VideoSendWorker.h"
#include "VideoEncoder.h"
#include "../net/Protocol.h"
#include <QVideoFrameFormat>
#include <QImage>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

// Опорный кадр каждые ~3 с (72 кадра при 24 к/с) — как у веба.
static const int kKeyEveryFrames = 72;

// Пиксельный формат Qt -> FFmpeg для прямой конверсии без промежуточной QImage.
// YV12 отдаём как YUV420P с переставленными U/V (см. encode()).
static AVPixelFormat toAvPixFmt(QVideoFrameFormat::PixelFormat f) {
    switch (f) {
    case QVideoFrameFormat::Format_YUV420P:
    case QVideoFrameFormat::Format_YV12:  return AV_PIX_FMT_YUV420P;
    case QVideoFrameFormat::Format_NV12:  return AV_PIX_FMT_NV12;
    case QVideoFrameFormat::Format_NV21:  return AV_PIX_FMT_NV21;
    case QVideoFrameFormat::Format_YUYV:  return AV_PIX_FMT_YUYV422;
    case QVideoFrameFormat::Format_UYVY:  return AV_PIX_FMT_UYVY422;
    default:                              return AV_PIX_FMT_NONE;
    }
}

VideoSendWorker::VideoSendWorker(QObject* parent) : QObject(parent) {}

VideoSendWorker::~VideoSendWorker() {
    delete m_enc;
    if (m_sws) sws_freeContext(m_sws);
}

void VideoSendWorker::reset() {
    delete m_enc;
    m_enc = nullptr;
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    m_frames = 0;
    m_keyNext = true;
    m_sendStartMs = 0;
}

void VideoSendWorker::encode(const QVideoFrame& frame, int maxW, int maxH,
                             int fps, int bitrate, bool forceKey, qint64 tsMs) {
    // Целевой размер: вписать захваченное в рамку пресета (камера могла дать
    // больше запрошенного), размеры чётные (§5.5).
    const int sw = frame.width(), sh = frame.height();
    if (sw < 2 || sh < 2) return;
    const qreal scale = qMin(1.0, qMin(qreal(maxW) / sw, qreal(maxH) / sh));
    const int tw = int(sw * scale) & ~1;
    const int th = int(sh * scale) & ~1;
    if (tw < 2 || th < 2) return;

    if (!m_enc || m_enc->width() != tw || m_enc->height() != th) {
        delete m_enc;
        m_enc = new VideoEncoder;
        if (!m_enc->open(tw, th, fps, bitrate)) {
            delete m_enc;                  // энкодеров нет — молча не вещаем
            m_enc = nullptr;
            return;
        }
        m_frames = 0;
        m_keyNext = true;
    }
    if (forceKey) m_keyNext = true;

    // Кадр камеры -> YUV420P кадра кодера. map() отдаёт планы в системной
    // памяти; форматы мимо списка (GPU-текстуры и пр.) едут через toImage().
    QVideoFrame src(frame);
    AVFrame* dst = m_enc->frame();
    if (!dst) return;

    bool converted = false;
    const AVPixelFormat srcFmt = toAvPixFmt(frame.pixelFormat());
    if (srcFmt != AV_PIX_FMT_NONE && src.map(QVideoFrame::ReadOnly)) {
        const uint8_t* data[4] = {};
        int stride[4] = {};
        for (int i = 0; i < src.planeCount() && i < 4; ++i) {
            data[i] = src.bits(i);
            stride[i] = src.bytesPerLine(i);
        }
        if (frame.pixelFormat() == QVideoFrameFormat::Format_YV12)
            qSwap(data[1], data[2]);       // YV12: V раньше U
        m_sws = sws_getCachedContext(m_sws, sw, sh, srcFmt,
                                     tw, th, AV_PIX_FMT_YUV420P,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (m_sws) {
            sws_scale(m_sws, data, stride, 0, sh, dst->data, dst->linesize);
            converted = true;
        }
        src.unmap();
    }
    if (!converted) {
        QImage img = frame.toImage();
        if (img.isNull()) return;
        img = img.convertToFormat(QImage::Format_RGBA8888);
        const uint8_t* data[4] = { img.constBits(), nullptr, nullptr, nullptr };
        const int stride[4] = { int(img.bytesPerLine()), 0, 0, 0 };
        m_sws = sws_getCachedContext(m_sws, img.width(), img.height(),
                                     AV_PIX_FMT_RGBA,
                                     tw, th, AV_PIX_FMT_YUV420P,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_sws) return;
        sws_scale(m_sws, data, stride, 0, img.height(), dst->data, dst->linesize);
    }

    const bool key = m_keyNext || (m_frames % kKeyEveryFrames == 0);
    m_keyNext = false;
    m_frames++;

    // PTS кодера — своя монотонная шкала; наружу в заголовке уходит tsMs
    // (Date.now() отправителя) — общая шкала с аудио, по ней приёмник
    // синхронизирует губы (§5.3).
    if (m_sendStartMs == 0) m_sendStartMs = tsMs;
    const auto packets = m_enc->encode(key, tsMs - m_sendStartMs);
    for (const VideoEncoder::Packet& p : packets) {
        emit packetReady(Proto::pack(
            Proto::VIDEO_CODED,
            p.key ? Proto::FLAG_KEYFRAME : 0,
            m_enc->protoCodec(),
            quint64(tsMs),
            p.data));
    }
}
