#include "VideoSendWorker.h"
#include "VideoEncoder.h"
#include "../net/Protocol.h"
#include "../crypto/E2eCipher.h"
#include <QVideoFrameFormat>
#include <QImage>
#include <QThread>
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

VideoSendWorker::VideoSendWorker(quint8 msgType, E2eCipher* cipher, QObject* parent)
    : QObject(parent), m_msgType(msgType), m_cipher(cipher),
      // Камера — опорный кадр раз в ~3 с (72 кадра при 24 к/с), как у веба.
      //
      // Экран — раз в ~30 с (900 кадров при 30 к/с). Здесь было 240, то есть
      // каждые восемь секунд, и это регулярно вредило: опорный кадр экрана —
      // до четверти мегабайта, он одним всплеском пробивал порог затора, после
      // чего кадры выбрасывались пачкой. А нужен он редко: транспорт у нас TCP,
      // по дороге не теряется ничего, и приёмнику опорный кадр требуется только
      // на входе в комнату (шлём сразу) или если его декодер сбился (просит
      // сам, KEYFRAME_REQ). Тридцать секунд оставлены страховкой на случай, когда
      // просьба почему-то не дошла, — чтобы картинка починилась сама.
      m_keyEvery(msgType == Proto::SCREEN_CODED ? 900 : 72) {}

VideoSendWorker::~VideoSendWorker() {
    delete m_enc;
    if (m_sws) sws_freeContext(m_sws);
}

void VideoSendWorker::reset() {
    delete m_enc;
    m_enc = nullptr;
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    m_swsSrcW = m_swsSrcH = m_swsDstW = m_swsDstH = 0;
    m_swsSrcFmt = m_swsDstFmt = -1;
    m_frames = 0;
    m_keyNext = true;
    m_sendStartMs = 0;
    // Измерение частоты относится к прошлому потоку кадров: у нового источника
    // (другой монитор, другой пресет) она своя, и тащить туда старое среднее
    // значило бы открыть кодировщик под чужое число.
    m_lastEncodeMs = 0;
    m_ivlEmaMs = 0;
    m_ratedFrames = 0;
    m_openedFps = 0;
    m_openedBitrate = 0;
    m_openedAtMs = 0;
}

// Масштабатор собираем руками, а не через sws_getCachedContext: тому нельзя
// передать число потоков, а именно оно здесь решает. Перевод BGRA 4К в
// YUV420P 720p в один поток — 12–20 мс, то есть больше целого кадрового
// бюджета на 60 к/с; в четыре потока он укладывается с запасом.
bool VideoSendWorker::ensureSws(int sw, int sh, int srcFmt, int tw, int th, int dstFmt) {
    if (m_sws && sw == m_swsSrcW && sh == m_swsSrcH && srcFmt == m_swsSrcFmt
        && tw == m_swsDstW && th == m_swsDstH && dstFmt == m_swsDstFmt)
        return true;

    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    SwsContext* c = sws_alloc_context();
    if (!c) return false;

    c->src_w = sw;   c->src_h = sh;   c->src_format = srcFmt;
    c->dst_w = tw;   c->dst_h = th;   c->dst_format = dstFmt;
    // При уменьшении — усреднение по площади: билинейная выборка берёт 2×2
    // пикселя независимо от кратности, и текст после сжатия 4К в 720p
    // рассыпается в кашу. Ради демонстрации экрана это главный критерий, а
    // площадное усреднение дороже билинейного примерно вдвое — что как раз и
    // окупается потоками ниже. Когда уменьшать нечего, разницы нет.
    const bool downscale = (tw < sw || th < sh);
    c->flags = downscale ? SWS_AREA : SWS_BILINEAR;
    // Больше четырёх нарезок смысла не имеет: выигрыш выходит на полку, а
    // потоков в приложении и так шесть.
    c->threads = qBound(1, QThread::idealThreadCount() / 2, 4);

    if (sws_init_context(c, nullptr, nullptr) < 0) { sws_freeContext(c); return false; }

    m_sws = c;
    m_swsSrcW = sw; m_swsSrcH = sh; m_swsSrcFmt = srcFmt;
    m_swsDstW = tw; m_swsDstH = th; m_swsDstFmt = dstFmt;
    return true;
}

// Интервалы между кадрами, которые дошли до кодирования. Кадры, выброшенные
// движком (затор в сокете, воркер занят), сюда не попадают — и правильно:
// нас интересует частота, которую видит получатель.
void VideoSendWorker::noteInterval(qint64 tsMs, int requestedFps) {
    const qint64 period = qMax<qint64>(1, 1000 / qMax(1, requestedFps));
    const qint64 ivl = tsMs - m_lastEncodeMs;
    m_lastEncodeMs = tsMs;
    if (ivl <= 0) return;
    // Отделяем «источник молчал» от «кодировщик не поспевает»: статичный экран
    // не порождает кадров вовсе, и такую паузу в среднее брать нельзя — иначе
    // после каждого затишья мы объявляли бы кодеру частоту втрое ниже реальной.
    // Порог ведём и от пресета, и от уже измеренного темпа: если мы честно
    // отдаём 12 к/с, интервал 83 мс — это норма, а не пауза, и выкинув его мы
    // застыли бы на завышенной оценке навсегда.
    const qint64 limit = qMax<qint64>(period * 4, qint64(m_ivlEmaMs * 3));
    if (ivl > limit) return;
    m_ivlEmaMs = m_ivlEmaMs > 0 ? m_ivlEmaMs * 0.9 + double(ivl) * 0.1 : double(ivl);
    ++m_ratedFrames;
}

// Частота, под которую честно открывать кодировщик. Пока измерений мало,
// верим пресету; дальше — тому, что получается, но не выше запрошенного.
int VideoSendWorker::achievedFps(int requestedFps) const {
    if (m_ratedFrames < 30 || m_ivlEmaMs <= 0) return requestedFps;
    return qBound(5, int(qRound(1000.0 / m_ivlEmaMs)), requestedFps);
}

// Пропуска неизменившихся кадров здесь НЕТ, и это осознанно. Он тут был:
// захват отдаёт кадр на каждый тик, менялось что-то или нет (на замере 61 %
// кадров были байт-в-байт предыдущими), и отбрасывать их выглядело чистой
// экономией. На деле от этого поехала сама передача.
//
// Причина — конвейер аппаратного кодировщика глубиной два кадра. Пакет выходит
// из него не на том вызове, на котором кадр туда положили, а через два. Пока
// кадры идут потоком, это просто задержка; а вот стоит потоку прерваться —
// последние два кадра остаются ВНУТРИ кодировщика, и получатель продолжает
// видеть картинку, какой она была до остановки. Проще всего это заметить,
// перестав крутить страницу: у зрителя ещё пару секунд прежнее содержимое.
// Плюс частота отправки на статике падала до нуля, и «Диагностика» выглядела
// сломанной.
//
// Ровный поток кадров стоит тех кадров, которые он тратит зря: при аппаратном
// кодировании кадр обходится ~2.5 мс, а предсказуемость важнее. Если экономия
// понадобится — правильное место для неё не отпечаток кадра, а dirty rects от
// собственного захвата, где «не изменилось» известно точно и бесплатно.
//
// Дорисовки курсора здесь тоже больше НЕТ. Она была нужна, пока захват шёл
// через Qt: Desktop Duplication отдаёт кадр без курсора, и его приходилось
// накладывать самим — со своим масштабированием, своим сопоставлением
// монитора и своими ошибками в обоих. Теперь захват наш (ScreenCapturer), а
// Windows.Graphics.Capture рисует курсор сама по флагу IsCursorCaptureEnabled.

void VideoSendWorker::setCodecStep(int step) {
    const int s = qBound(0, step, 2);
    if (m_codecStep == s) return;
    m_codecStep = s;
    reset();                       // энкодер переоткроется на следующем кадре
}

// Обёртка: что бы внутри ни случилось, счётчик занятости должен вернуться к
// нулю, — иначе движок перестанет слать кадры совсем.
void VideoSendWorker::encode(const QVideoFrame& frame, int maxW, int maxH,
                             int fps, int bitrate, bool forceKey, qint64 tsMs) {
    encodeFrame(frame, maxW, maxH, fps, bitrate, forceKey, tsMs);
    m_inFlight.fetch_sub(1, std::memory_order_release);
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

    if (m_lastEncodeMs) noteInterval(tsMs, fps);
    else                m_lastEncodeMs = tsMs;
    const int useFps = achievedFps(fps);

    // Кодировщик делит битрейт на объявленную частоту. Пока мы говорили ему
    // пресетные 60, а отдавали 20, каждому реальному кадру доставалась треть
    // положенных бит — картинка мылилась при формально высоком битрейте.
    // Разошлись больше чем на четверть — переоткрываем, но не чаще раза в
    // четыре секунды: переоткрытие стоит опорного кадра, то есть заметного
    // всплеска в сокете.
    // Выдержка НАМНОГО длиннее, чем у битрейта ниже, и это не симметрия ради
    // симметрии. Точность объявленной частоты влияет только на то, как кодер
    // раскладывает биты, — ошибка здесь означает «чуть мягче картинка», и
    // терпеть её можно долго. А каждое переоткрытие стоит опорного кадра, то
    // есть всплеска в сокете. С выдержкой 4 с на дрожащем источнике (а захват
    // экрана именно такой) переоткрытия шли пачками: на замере два за 17 с,
    // 30 -> 16 -> 15 к/с. Пятнадцать секунд дают частоте устояться.
    const bool fpsStale = m_enc && m_openedFps > 0
        && qAbs(useFps - m_openedFps) * 4 > m_openedFps
        && tsMs - m_openedAtMs > 15000;
    // Битрейт — другое дело: его двигает регулятор качества
    // (VideoEngine::screenBitrate), и двигает потому, что канал уже не тянет.
    // Тут медлить нельзя, поэтому выдержка короткая. Порог в четверть выбран
    // под ступени регулятора: они отстоят друг от друга примерно на 30 %, так
    // что каждая смена ступени сюда доходит, а дребезг — нет.
    const bool bitrateStale = m_enc && m_openedBitrate > 0
        && qAbs(bitrate - m_openedBitrate) * 4 > m_openedBitrate
        && tsMs - m_openedAtMs > 4000;

    if (!m_enc || m_enc->width() != tw || m_enc->height() != th
        || fpsStale || bitrateStale) {
        delete m_enc;
        m_enc = new VideoEncoder;
        // Полоса решает, по какой лестнице спускаться: у демонстрации она
        // упирается в видеокарту, у камеры — в процессор (см. VideoEncoder::open).
        const bool screen = (m_msgType == Proto::SCREEN_CODED);
        if (!m_enc->open(tw, th, useFps, bitrate, screen, m_codecStep)) {
            delete m_enc;                  // энкодеров нет — молча не вещаем
            m_enc = nullptr;
            return;
        }
        m_openedFps = useFps;
        m_openedBitrate = bitrate;
        m_openedAtMs = tsMs;
        emit encoderOpened(m_enc->protoCodec(), tw, th, m_enc->isHardware());
        m_frames = 0;
        m_keyNext = true;
    }
    if (forceKey) m_keyNext = true;

    // Кадр камеры -> кадр кодера (YUV420P у программных, NV12 у аппаратного).
    // map() отдаёт планы в системной памяти; форматы мимо списка (GPU-текстуры
    // и пр.) едут через toImage().
    QVideoFrame src(frame);
    AVFrame* dst = m_enc->frame();
    if (!dst) return;
    const int dstFmt = m_enc->pixFmt();

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
        if (ensureSws(sw, sh, srcFmt, tw, th, dstFmt)) {
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
        if (!ensureSws(img.width(), img.height(), AV_PIX_FMT_RGBA, tw, th, dstFmt)) return;
        sws_scale(m_sws, data, stride, 0, img.height(), dst->data, dst->linesize);
    }

    const bool key = m_keyNext || (m_frames % m_keyEvery == 0);
    m_keyNext = false;
    m_frames++;

    // PTS кодера — своя монотонная шкала; наружу в заголовке уходит время
    // съёмки (Date.now() отправителя) — общая шкала с аудио, по ней приёмник
    // синхронизирует губы (§5.3). Обратно его считаем из PTS пакета, а не
    // берём tsMs: у аппаратного кодировщика пакет относится к кадру, который
    // положили двумя вызовами раньше, и tsMs описывал бы уже не его.
    if (m_sendStartMs == 0) m_sendStartMs = tsMs;
    const auto packets = m_enc->encode(key, tsMs - m_sendStartMs);
    for (const VideoEncoder::Packet& p : packets) {
        const qint64 frameTsMs = m_sendStartMs + p.ptsMs;
        // Сквозное шифрование: наружу уходит только запечатанный payload,
        // заголовок остаётся открытым — по нему сервер релеит полосу. Не
        // смогли запечатать — кадр не уходит вовсе: отдать картинку открытой
        // при включённом ключе хуже, чем пропустить кадр.
        quint8 flags = p.key ? Proto::FLAG_KEYFRAME : 0;
        QByteArray body = p.data;
        if (m_cipher->active()) {
            body = m_cipher->seal(m_msgType, m_enc->protoCodec(), body);
            if (body.isEmpty()) continue;
            flags |= Proto::FLAG_ENCRYPTED;
        }
        const QByteArray packet = Proto::pack(
            m_msgType,
            flags,
            m_enc->protoCodec(),
            quint64(frameTsMs),
            body);
        emit packetReady(packet);          // -> поток сокета
        emit packetSent(packet.size());    // -> GUI-поток, только число
    }
}
