#include "VideoSendWorker.h"
#include "VideoEncoder.h"
#include "ScreenCursor.h"
#include "../net/Protocol.h"
#include <QVideoFrameFormat>
#include <QImage>
#include <QtMath>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

// Пиксельный формат Qt -> FFmpeg для прямой конверсии без промежуточной QImage.
// YV12 отдаём как YUV420P с переставленными U/V (см. encode()).
//
// Список RGB обязателен: захват экрана на Windows приходит в BGRA8888, и без
// этой строки кадр уезжал в запасной путь через toImage() —
// на 4K это 17 мс на снимок плюс ещё одна полная копия под convertToFormat,
// каждый кадр. Это и была главная причина, по которой картинка отставала от
// звука при включённой демонстрации.
static AVPixelFormat toAvPixFmt(QVideoFrameFormat::PixelFormat f) {
    switch (f) {
    case QVideoFrameFormat::Format_YUV420P:
    case QVideoFrameFormat::Format_YV12:  return AV_PIX_FMT_YUV420P;
    case QVideoFrameFormat::Format_NV12:  return AV_PIX_FMT_NV12;
    case QVideoFrameFormat::Format_NV21:  return AV_PIX_FMT_NV21;
    case QVideoFrameFormat::Format_YUYV:  return AV_PIX_FMT_YUYV422;
    case QVideoFrameFormat::Format_UYVY:  return AV_PIX_FMT_UYVY422;
    case QVideoFrameFormat::Format_BGRA8888:
    case QVideoFrameFormat::Format_BGRA8888_Premultiplied: return AV_PIX_FMT_BGRA;
    case QVideoFrameFormat::Format_BGRX8888: return AV_PIX_FMT_BGR0;
    case QVideoFrameFormat::Format_RGBA8888: return AV_PIX_FMT_RGBA;
    case QVideoFrameFormat::Format_RGBX8888: return AV_PIX_FMT_RGB0;
    case QVideoFrameFormat::Format_ARGB8888:
    case QVideoFrameFormat::Format_ARGB8888_Premultiplied: return AV_PIX_FMT_ARGB;
    case QVideoFrameFormat::Format_XRGB8888: return AV_PIX_FMT_0RGB;
    case QVideoFrameFormat::Format_ABGR8888: return AV_PIX_FMT_ABGR;
    case QVideoFrameFormat::Format_XBGR8888: return AV_PIX_FMT_0BGR;
    default:                              return AV_PIX_FMT_NONE;
    }
}

VideoSendWorker::VideoSendWorker(quint8 msgType, QObject* parent)
    : QObject(parent), m_msgType(msgType),
      // Камера — опорный кадр раз в ~3 с (72 кадра при 24 к/с), как у веба.
      // Экран — вчетверо реже: его опорный кадр в разы тяжелее (крупная
      // статичная картинка), и частые всплески забивают сокет, задерживая
      // за собой звук. Новичку и по просьбе кадр всё равно шлём немедленно.
      m_keyEvery(msgType == Proto::SCREEN_CODED ? 240 : 72) {}

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

void VideoSendWorker::setCursorSource(const QRect& physicalRect) {
    m_cursorSrc = physicalRect;
}

// Курсор поверх готового кадра YUV420P. Рисуем ПОСЛЕ масштабирования: так
// стрелка остаётся резкой и не мельчает вместе с картинкой — на 360p её
// «настоящий» размер был бы пять пикселей, поэтому есть нижняя граница.
// Коэффициенты BT.601 — те же, по которым sws_scale переводил кадр в YUV.
void VideoSendWorker::blendCursor(AVFrame* dst, int tw, int th) {
    if (m_cursorSrc.isEmpty() || !dst) return;
    const ScreenCursor cur = ScreenCursor::grab();
    if (cur.isNull()) return;

    const QPoint rel = cur.posPhysical - m_cursorSrc.topLeft();
    if (rel.x() < 0 || rel.y() < 0 ||
        rel.x() >= m_cursorSrc.width() || rel.y() >= m_cursorSrc.height()) return;

    const double sx = double(tw) / m_cursorSrc.width();
    const double sy = double(th) / m_cursorSrc.height();
    // Пропорции сохраняем, но не даём стрелке стать меньше 16 пикселей.
    const double f = qMax(qMin(sx, sy), 16.0 / qMax(1, cur.image.width()));
    const int cw = qMax(1, int(qRound(cur.image.width() * f)));
    const int ch = qMax(1, int(qRound(cur.image.height() * f)));
    const QImage img = (cw == cur.image.width() && ch == cur.image.height())
        ? cur.image
        : cur.image.scaled(cw, ch, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    const int x0 = int(qRound(rel.x() * sx - cur.hotspot.x() * f));
    const int y0 = int(qRound(rel.y() * sy - cur.hotspot.y() * f));

    uint8_t* yPlane = dst->data[0];
    uint8_t* uPlane = dst->data[1];
    uint8_t* vPlane = dst->data[2];
    if (!yPlane || !uPlane || !vPlane) return;

    // Яркость — по каждому пикселю.
    for (int y = 0; y < img.height(); ++y) {
        const int py = y0 + y;
        if (py < 0 || py >= th) continue;
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        uint8_t* yRow = yPlane + qsizetype(py) * dst->linesize[0];
        for (int x = 0; x < img.width(); ++x) {
            const int px = x0 + x;
            if (px < 0 || px >= tw) continue;
            const int a = qAlpha(row[x]);
            if (a == 0) continue;
            const int lum = (77 * qRed(row[x]) + 150 * qGreen(row[x])
                             + 29 * qBlue(row[x])) >> 8;
            yRow[px] = uint8_t((lum * a + yRow[px] * (255 - a)) / 255);
        }
    }

    // Цветность — вчетверо реже, по сетке 2x2 исходного кадра.
    const int cw2 = (tw + 1) / 2, ch2 = (th + 1) / 2;
    for (int cy = qMax(0, y0 / 2); cy < ch2; ++cy) {
        const int sampleY = cy * 2 - y0;
        if (sampleY >= img.height()) break;
        if (sampleY < 0) continue;
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(sampleY));
        uint8_t* uRow = uPlane + qsizetype(cy) * dst->linesize[1];
        uint8_t* vRow = vPlane + qsizetype(cy) * dst->linesize[2];
        for (int cx = qMax(0, x0 / 2); cx < cw2; ++cx) {
            const int sampleX = cx * 2 - x0;
            if (sampleX >= img.width()) break;
            if (sampleX < 0) continue;
            const int a = qAlpha(row[sampleX]);
            if (a == 0) continue;
            const int r = qRed(row[sampleX]), g = qGreen(row[sampleX]), b = qBlue(row[sampleX]);
            const int u = qBound(0, 128 + ((-43 * r - 85 * g + 128 * b) >> 8), 255);
            const int v = qBound(0, 128 + ((128 * r - 107 * g - 21 * b) >> 8), 255);
            uRow[cx] = uint8_t((u * a + uRow[cx] * (255 - a)) / 255);
            vRow[cx] = uint8_t((v * a + vRow[cx] * (255 - a)) / 255);
        }
    }
}

// Обёртка: что бы внутри ни случилось, движок должен узнать, что воркер
// освободился, — иначе он перестанет слать кадры совсем.
void VideoSendWorker::encode(const QVideoFrame& frame, int maxW, int maxH,
                             int fps, int bitrate, bool forceKey, qint64 tsMs) {
    encodeFrame(frame, maxW, maxH, fps, bitrate, forceKey, tsMs);
    emit frameDone();
}

void VideoSendWorker::encodeFrame(const QVideoFrame& frame, int maxW, int maxH,
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

    blendCursor(dst, tw, th);      // курсор рисуем уже по месту, поверх кадра

    const bool key = m_keyNext || (m_frames % m_keyEvery == 0);
    m_keyNext = false;
    m_frames++;

    // PTS кодера — своя монотонная шкала; наружу в заголовке уходит tsMs
    // (Date.now() отправителя) — общая шкала с аудио, по ней приёмник
    // синхронизирует губы (§5.3).
    if (m_sendStartMs == 0) m_sendStartMs = tsMs;
    const auto packets = m_enc->encode(key, tsMs - m_sendStartMs);
    for (const VideoEncoder::Packet& p : packets) {
        emit packetReady(Proto::pack(
            m_msgType,
            p.key ? Proto::FLAG_KEYFRAME : 0,
            m_enc->protoCodec(),
            quint64(tsMs),
            p.data));
    }
}
