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

// Общая проверка «есть ли железо под этот кодек». Пробуем именно ОТКРЫТЬ его с
// hw_encoding=1: имя в сборке FFmpeg есть всегда, а видеокарта, умеющая им
// кодировать, — нет. Программный вариант нас не устраивает ни в одном случае
// (он медленнее openh264), поэтому «нет железа» = «пункта нет».
static bool probeHardware(const char* encoderName, AVCodecID decoderId,
                          const char* human) {
    int ok = 0;
    if (const AVCodec* c = avcodec_find_encoder_by_name(encoderName)) {
        AVCodecContext* ctx = avcodec_alloc_context3(c);
        if (ctx) {
            // Размер маленький намеренно: проверяем наличие железа, а не его
            // производительность, и платить за это созданием кадра 4К незачем.
            ctx->width = 640;
            ctx->height = 360;
            ctx->pix_fmt = AV_PIX_FMT_NV12;
            ctx->time_base = { 1, 1000 };
            ctx->framerate = { 30, 1 };
            ctx->bit_rate = 1000000;
            AVDictionary* opts = nullptr;
            av_dict_set(&opts, "hw_encoding", "1", 0);
            const int rc = avcodec_open2(ctx, c, &opts);
            av_dict_free(&opts);
            ok = (rc >= 0) ? 1 : 0;
            avcodec_free_context(&ctx);
        }
    }
    // Уметь кодировать мало: если наш же декодер не соберётся, два наших
    // клиента друг друга не увидят. Предлагать такой выбор нечестно.
    if (ok && !avcodec_find_decoder(decoderId)) ok = 0;
    qInfo() << "VideoEncoder: аппаратный" << human << (ok ? "доступен" : "недоступен");
    return ok != 0;
}

bool VideoEncoder::hardwareHevcAvailable() {
    static const bool ok = probeHardware("hevc_mf", AV_CODEC_ID_HEVC, "HEVC");
    return ok;
}

bool VideoEncoder::hardwareAv1Available() {
    static const bool ok = probeHardware("av1_mf", AV_CODEC_ID_AV1, "AV1");
    return ok;
}

bool VideoEncoder::tryOpen(const Candidate& cand,
                           int width, int height, int fps, int bitrate) {
    const AVCodec* codec = avcodec_find_encoder_by_name(cand.name);
    if (!codec) return false;

    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx) return false;

    m_ctx->width = width;
    m_ctx->height = height;
    m_ctx->pix_fmt = AVPixelFormat(cand.pixFmt);
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
    if (cand.hardware) {
        // Media Foundation. Своих опций у обёртки ровно четыре (проверено
        // пробой: profile/level/low_latency она не знает вовсе и молча
        // игнорирует — уровень она считает сама по размеру кадра и частоте,
        // и считает верно: 4.0 на 720p60, 4.2 на 1080p60, 5.1 на 4К30).
        //   scenario=display_remoting — режим «удалённый рабочий стол»: именно
        //     под экранный контент, а не под говорящую голову.
        //   rate_control=pc_vbr — VBR с ограничением пика. Здесь был cbr, и это
        //     была ошибка: CBR добивает поток до целевого битрейта независимо от
        //     содержимого, а демонстрация экрана почти всегда неподвижна. На
        //     статичной картинке 1080p замер дал 3414 кбит/с против 714 у
        //     pc_vbr — впятеро больше ни за что. Режим quality на статике ещё
        //     скромнее (570), но на движении вылетает за лимит (3961 при цели
        //     3500), а превышение — это ровно то, что пробивает порог затора в
        //     сокете и оборачивается замиранием. pc_vbr держит и то и другое:
        //     714 на статике, 3614 на движении.
        //   hw_encoding=1 — только видеокарта. Если её кодировщика нет,
        //     открытие ПРОВАЛИТСЯ, и мы уйдём на libopenh264 ниже. Так и надо:
        //     программный кодировщик внутри MF медленнее openh264, и получить
        //     его молча было бы хуже, чем не получить MF вовсе.
        av_dict_set(&opts, "scenario", "display_remoting", 0);
        av_dict_set(&opts, "rate_control", "pc_vbr", 0);
        av_dict_set(&opts, "hw_encoding", "1", 0);
    } else if (cand.proto == Proto::CODEC_H264) {
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
        if (cand.proto == Proto::CODEC_VP9) {
            // VP9 заметно тяжелее VP8: без распараллеливания по строкам и
            // колонкам тайлов он на 1080p не укладывается в кадр.
            av_dict_set(&opts, "row-mt", "1", 0);
            av_dict_set(&opts, "tile-columns", "2", 0);
        }
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
    m_frame->format = cand.pixFmt;
    m_frame->width = width;
    m_frame->height = height;
    if (av_frame_get_buffer(m_frame, 0) < 0) { close(); return false; }

    m_protoCodec = cand.proto;
    m_pixFmt = cand.pixFmt;
    m_hardware = cand.hardware;
    m_width = width;
    m_height = height;
    qInfo() << "VideoEncoder:" << cand.name << width << "x" << height
            << fps << "fps" << bitrate << "bps"
            << (cand.hardware ? "(hardware)" : "(software)");
    return true;
}

bool VideoEncoder::open(int width, int height, int fps, int bitrate,
                        quint8 preferred, bool allowHardware) {
    close();
    width &= ~1;                             // чётные размеры — правило §5.5
    height &= ~1;
    if (width < 2 || height < 2) return false;

    // Аппаратный H.264 и программный H.264 — ДВА кандидата с одним и тем же
    // кодеком протокола. Поэтому отбор дальше идёт по имени кодировщика: будь
    // он по Proto::CODEC_*, второй H.264 отбрасывался бы как дубликат — то
    // есть запасного пути у аппаратного не осталось бы вовсе.
    static const Candidate kH264Hw{ "h264_mf",     Proto::CODEC_H264, AV_PIX_FMT_NV12,    true };
    static const Candidate kHevcHw{ "hevc_mf",     Proto::CODEC_HEVC, AV_PIX_FMT_NV12,    true };
    static const Candidate kAv1Hw { "av1_mf",      Proto::CODEC_AV1,  AV_PIX_FMT_NV12,    true };
    static const Candidate kH264Sw{ "libopenh264", Proto::CODEC_H264, AV_PIX_FMT_YUV420P, false };
    static const Candidate kVp8   { "libvpx",      Proto::CODEC_VP8,  AV_PIX_FMT_YUV420P, false };
    static const Candidate kVp9   { "libvpx-vp9",  Proto::CODEC_VP9,  AV_PIX_FMT_YUV420P, false };

    QList<Candidate> order;
    const auto add = [&order](const Candidate& c) {
        for (const Candidate& x : order) if (qstrcmp(x.name, c.name) == 0) return;
        order.append(c);
    };

    // Сначала — выбор пользователя. Для H.264 это значит «сперва видеокарта,
    // потом процессор»: и то и другое для приёмника один и тот же H.264.
    if (preferred == Proto::CODEC_H264) {
        if (allowHardware) add(kH264Hw);
        add(kH264Sw);
    } else if (preferred == Proto::CODEC_HEVC) {
        // Только аппаратный и только по явной просьбе. В запасные HEVC не
        // попадает НИКОГДА: получить патентованный кодек случайно, не выбирая
        // его, не должен никто. Если железа нет — уйдём на H.264 ниже, и
        // человек об этом узнает (codecFallback).
        if (allowHardware) add(kHevcHw);
    } else if (preferred == Proto::CODEC_AV1) {
        // Как и HEVC: только аппаратный и только по явной просьбе. В
        // запасные не попадает — принимать AV1 умеют далеко не все, и
        // получить его молча значило бы выключить часть участников.
        if (allowHardware) add(kAv1Hw);
    } else if (preferred == Proto::CODEC_VP8) {
        add(kVp8);
    } else if (preferred == Proto::CODEC_VP9) {
        // VP9 в запасные не входит: он дороже по процессору, и получить его
        // случайно, не выбирая, никто не должен.
        add(kVp9);
    }
    // Дальше — порядок по умолчанию, как у веба: H.264 (аппаратный декод почти
    // у всех приёмников), затем VP8. Все уходят одним протоколом v2.
    if (allowHardware) add(kH264Hw);
    add(kH264Sw);
    add(kVp8);

    for (const Candidate& c : order)
        if (tryOpen(c, width, height, fps, bitrate)) return true;

    qWarning() << "VideoEncoder: в сборке FFmpeg нет ни одного пригодного энкодера";
    return false;
}

void VideoEncoder::close() {
    if (m_pkt) av_packet_free(&m_pkt);
    if (m_frame) av_frame_free(&m_frame);
    if (m_ctx) avcodec_free_context(&m_ctx);
    m_protoCodec = 0;
    m_width = m_height = 0;
    m_pixFmt = 0;
    m_hardware = false;
}

AVFrame* VideoEncoder::frame() {
    if (!m_frame) return nullptr;
    // Кадр мог остаться «в аренде» у кодера с прошлого encode() — вернуть себе.
    if (av_frame_make_writable(m_frame) < 0) return nullptr;
    return m_frame;
}

// Пометить поток H.264 как Constrained Baseline.
//
// Зачем. Аппаратный кодировщик AMD выдаёт SPS с profile_idc=66 и флагами
// ограничений 0x04 — это ОБЫЧНЫЙ Baseline. Он формально допускает FMO, ASO и
// избыточные срезы; в железе их нет, поэтому аппаратные декодеры такой поток
// отвергают целиком. Замер: наш собственный H.264 разбирался процессором за
// 3.10 мс на 1080p60, а после подъёма одного бита — видеокартой за 0.58.
// Впятеро, и это для КАЖДОГО зрителя, потому что H.264 у нас основной кодек.
//
// Честно ли это. Да: constraint_set1_flag означает «поток не пользуется
// средствами, запрещёнными в Main» — то есть теми самыми FMO/ASO. Аппаратные
// кодировщики их не умеют в принципе, так что мы не приписываем потоку
// свойство, которого у него нет, а сообщаем то, о чём кодировщик умолчал.
// Поэтому и правим только profile_idc=66: у Main и High этого бита нет смысла
// трогать, а у чужих потоков мы ничего не переписываем — только у своих.
//
// Байт флагов лежит через один после заголовка NAL и окружён ненулевыми
// байтами (profile_idc и level_idc), поэтому подстановочные нули Annex B тут
// не возникают и не ломаются.
static void markConstrainedBaseline(QByteArray& annexB) {
    const int n = annexB.size();
    for (int i = 0; i + 5 < n; ++i) {
        int sc = 0;
        if (quint8(annexB[i]) == 0 && quint8(annexB[i + 1]) == 0
            && quint8(annexB[i + 2]) == 1) sc = 3;
        else if (i + 6 < n && quint8(annexB[i]) == 0 && quint8(annexB[i + 1]) == 0
                 && quint8(annexB[i + 2]) == 0 && quint8(annexB[i + 3]) == 1) sc = 4;
        if (!sc) continue;
        if ((quint8(annexB[i + sc]) & 0x1F) != 7) continue;      // не SPS
        const int prof = i + sc + 1, flags = prof + 1;
        if (flags >= n) return;
        if (quint8(annexB[prof]) == 66)
            annexB[flags] = char(quint8(annexB[flags]) | 0x40);  // constraint_set1
        return;                                                  // SPS в кадре один
    }
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
        // SPS едет только в опорных кадрах (мы не ставим global_header — §6.1),
        // поэтому и искать его в остальных незачем.
        if (p.key && m_protoCodec == Proto::CODEC_H264) markConstrainedBaseline(p.data);
        // Метка ИМЕННО этого пакета. Аппаратный кодировщик держит конвейер на
        // два кадра, и без неё пакет уходил бы в сеть со временем кадра, что
        // сейчас положили на вход, — то есть на два кадра новее собственной
        // картинки. Программные кодеры отдают pts обратно как есть, так что
        // путь для них не меняется; NOPTS не отдаёт никто, но подстрахуемся.
        p.ptsMs = (m_pkt->pts == AV_NOPTS_VALUE) ? ptsMs : m_pkt->pts;
        out.append(p);
        av_packet_unref(m_pkt);
    }
    return out;
}
