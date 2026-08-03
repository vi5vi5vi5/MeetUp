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
        //     под экранный контент, а не под говорящую голову. Ставим его ВСЕМ,
        //     КРОМЕ h264_mf, и это не вкусовщина, а замер. У кодировщика H.264
        //     этот режим стоит 17-30 мс на кадр — и на 720p, и на 4К, то есть
        //     расход не зависит от числа пикселей и работой быть не может. При
        //     этом он не даёт ничего: тот же битрейт (5687 против 5687 кбит/с) и
        //     тот же PSNR (40.02 против 40.05 дБ). Тридцать миллисекунд ни за
        //     что — это потолок в 33 кадра в секунду на ровном месте, каким бы
        //     мелким ни было окно. Ровно об этот параметр и бился весь H.264.
        //     У hevc_mf и av1_mf режим бесплатен (1.4 мс), там он остаётся.
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
        if (cand.proto != Proto::CODEC_H264)
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
    // Сообщает об открытии не эта функция, а open(): сюда заходит ещё и проба
    // доступности (probe), и её попытки в логе выглядели бы как настоящие
    // запуски кодировщика — шесть штук подряд при каждом открытии настроек.
    return true;
}

// Аппаратный H.264 и программный H.264 — ДВА кандидата с одним и тем же
// кодеком протокола. Поэтому отбор идёт по имени кодировщика: будь он по
// Proto::CODEC_*, второй H.264 отбрасывался бы как дубликат — то есть
// запасного пути у аппаратного не осталось бы вовсе.
static const VideoEncoder::Candidate kHevcHw{ "hevc_mf",     Proto::CODEC_HEVC, AV_PIX_FMT_NV12,    true };
static const VideoEncoder::Candidate kAv1Hw { "av1_mf",      Proto::CODEC_AV1,  AV_PIX_FMT_NV12,    true };
static const VideoEncoder::Candidate kH264Hw{ "h264_mf",     Proto::CODEC_H264, AV_PIX_FMT_NV12,    true };
static const VideoEncoder::Candidate kH264Sw{ "libopenh264", Proto::CODEC_H264, AV_PIX_FMT_YUV420P, false };
static const VideoEncoder::Candidate kVp9   { "libvpx-vp9",  Proto::CODEC_VP9,  AV_PIX_FMT_YUV420P, false };
static const VideoEncoder::Candidate kVp8   { "libvpx",      Proto::CODEC_VP8,  AV_PIX_FMT_YUV420P, false };

// Все кандидаты в одном месте — по нему же строится и список в настройках.
static const VideoEncoder::Candidate* const kAll[] = {
    &kHevcHw, &kAv1Hw, &kH264Hw, &kH264Sw, &kVp9, &kVp8
};

static const VideoEncoder::Candidate* findCandidate(const QString& id) {
    if (id.isEmpty()) return nullptr;
    for (const VideoEncoder::Candidate* c : kAll)
        if (id == QLatin1String(c->name)) return c;
    return nullptr;
}

// Подписи человеку. «Видеокарта» и «процессор» здесь — не украшение: это
// главное, что отличает две строки с одинаковым названием кодека, и главное,
// что влияет на цену кадра.
const QList<VideoEncoder::Option>& VideoEncoder::catalog() {
    static const QList<Option> list = {
        { QStringLiteral("hevc_mf"),     QStringLiteral("HEVC · видеокарта"),
          Proto::CODEC_HEVC, true },
        { QStringLiteral("av1_mf"),      QStringLiteral("AV1 · видеокарта"),
          Proto::CODEC_AV1,  true },
        { QStringLiteral("h264_mf"),     QStringLiteral("H.264 · видеокарта"),
          Proto::CODEC_H264, true },
        { QStringLiteral("libopenh264"), QStringLiteral("H.264 · процессор"),
          Proto::CODEC_H264, false },
        { QStringLiteral("libvpx-vp9"),  QStringLiteral("VP9 · процессор"),
          Proto::CODEC_VP9,  false },
        { QStringLiteral("libvpx"),      QStringLiteral("VP8 · процессор"),
          Proto::CODEC_VP8,  false },
    };
    return list;
}

// Кадр для пробы маленький и частота низкая: нас интересует, поднимется ли
// кодировщик вообще, а не сколько он стоит. Открытие MFT — это десятки
// миллисекунд, и умножать их на размер кадра незачем.
bool VideoEncoder::probe(const QString& id) {
    const Candidate* c = findCandidate(id);
    if (!c) return false;
    VideoEncoder enc;
    return enc.tryOpen(*c, 640, 360, 30, 1000000);
}

bool VideoEncoder::open(int width, int height, int fps, int bitrate,
                        bool screen, int step, const QString& preferred) {
    close();
    width &= ~1;                             // чётные размеры — правило §5.5
    height &= ~1;
    if (width < 2 || height < 2) return false;

    QList<Candidate> order;
    const auto add = [&order](const Candidate& c) {
        for (const Candidate& x : order) if (qstrcmp(x.name, c.name) == 0) return;
        order.append(c);
    };

    // Выбор человека — нулевая ступень, впереди всей лестницы. Со ступени
    // первой и ниже его больше не пробуем: туда нас отправила жалоба
    // получателя, то есть ровно на этот кодек кто-то и пожаловался.
    if (step <= 0)
        if (const Candidate* c = findCandidate(preferred)) add(*c);

    // Лестница кодеков. Ступенька — это не предпочтение, а ответ на конкретное
    // событие: получатель прислал «не понимаю такой кодек», и мы спускаемся на
    // одну ниже. Сам по себе спуск не происходит никогда — наверху всегда
    // лучшее, что есть.
    //
    // Ступени внутри себя ещё и подстраховывают друг друга: если ступень не
    // открылась (нет железа, нет кодека в сборке), пробуем всё, что ниже.
    // Остаться совсем без картинки хуже, чем вещать не тем, чем хотелось.
    if (screen) {
        // Демонстрация. Всё, что можно, — на видеокарту: замер показал, что
        // программный H.264 на 1440p стоит 31 мс на кадр, а на 4К — 71, то
        // есть годится он только как последний довод.
        //   0. HEVC — основной. Втрое экономнее H.264 по каналу при том же
        //      качестве (4158 против 10832 кбит/с на 1080p) и той же цене
        //      кодирования. Железо старше 2015 года его уже умеет.
        //   1. H.264 — упор на совместимость: его понимают все, включая
        //      браузеры без HEVC. Сперва видеокарта, потом процессор — для
        //      получателя это один и тот же поток.
        //   2. VP8 — реликт на случай, которого мы не ждём.
        if (step <= 0) add(kHevcHw);
        if (step <= 1) { add(kH264Hw); add(kH264Sw); }
        add(kVp8);
    } else {
        // Камера. Остаётся на процессоре целиком, и намеренно: у аппаратного
        // кодировщика конвейер глубиной два кадра, на 24 к/с это лишние ~83 мс
        // прямо в расхождение с голосом. Кадр 720p и на процессоре кодируется
        // за единицы миллисекунд — узким местом камера не была никогда.
        //   0. VP9 — самый быстрый из программных (3.6 мс на 1080p против
        //      16.3 у openh264) и разбирается видеокартой у большинства.
        //   1. H.264 — база совместимости: программный, зато понимают все.
        //   2. VP8 — тот же реликт.
        if (step <= 0) add(kVp9);
        if (step <= 1) add(kH264Sw);
        add(kVp8);
    }

    for (const Candidate& c : order) {
        if (!tryOpen(c, width, height, fps, bitrate)) continue;
        qInfo() << "VideoEncoder:" << c.name << width << "x" << height
                << fps << "fps" << bitrate << "bps"
                << (c.hardware ? "(hardware)" : "(software)")
                << (preferred.isEmpty() ? "" : "выбран человеком:")
                << (preferred.isEmpty() ? QString() : preferred);
        return true;
    }

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
