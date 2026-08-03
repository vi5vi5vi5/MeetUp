#include "VideoRecvWorker.h"
#include "VideoDecoder.h"
#include "../net/Protocol.h"
#include "../crypto/E2eCipher.h"
#include <QVideoFrameFormat>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QDateTime>
#include <QDebug>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

VideoRecvWorker::VideoRecvWorker(quint8 codedType, quint8 jpegType, E2eCipher* cipher,
                                 QObject* parent)
    : QObject(parent), m_codedType(codedType), m_jpegType(jpegType), m_cipher(cipher) {}

VideoRecvWorker::~VideoRecvWorker() { shutdown(); }

// Таймер создаётся здесь, а не в конструкторе: конструктор выполняется на
// GUI-потоке (объект ещё не переехал), а таймер обязан принадлежать своему.
void VideoRecvWorker::init() {
    m_sweepTimer = new QTimer(this);
    m_sweepTimer->setInterval(1000);
    connect(m_sweepTimer, &QTimer::timeout, this, &VideoRecvWorker::sweep);
    m_sweepTimer->start();
}

void VideoRecvWorker::shutdown() {
    if (m_sweepTimer) m_sweepTimer->stop();
    reset();
}

void VideoRecvWorker::dropDecoder(Peer& p) {
    delete p.dec;
    p.dec = nullptr;
    if (p.sws) { sws_freeContext(p.sws); p.sws = nullptr; }
    // Измерения относились к прошлому декодеру: у нового может быть и другой
    // кодек, и другой размер кадра, и другой ответ про видеокарту.
    p.annW = p.annH = 0;
    p.annHw = false;
    p.decEmaUs = 0;
    p.ivlEmaMs = 0;
    p.prevAt = 0;
    p.slowRun = 0;
    p.complainedSlow = false;
}

void VideoRecvWorker::reset() {
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        dropDecoder(*it);
        // Замок снимаем явно: плитка в интерфейсе переживёт этот сброс и сама
        // о нём не узнает.
        setLocked(*it, it.key(), false);
    }
    m_peers.clear();
}

void VideoRecvWorker::dropPeer(quint32 sender) {
    auto it = m_peers.find(sender);
    if (it == m_peers.end()) return;
    dropDecoder(*it);
    setLocked(*it, sender, false);
    m_peers.erase(it);
}

// Плитки нет уже давно (участник ушёл со страницы сетки, свернули сцену):
// декодер держим ровно столько, чтобы пережить пересоздание плитки, а не всю
// конференцию — состояние H.264-декодера 1080p стоит мегабайты.
void VideoRecvWorker::sweep() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        Peer& p = it.value();
        if (!p.wanted && p.dec && now - p.lastAt > 5000) {
            dropDecoder(p);
            setAwaitKey(p, it.key(), true);
        }
    }
}

void VideoRecvWorker::setWanted(quint32 sender, bool wanted) {
    Peer& p = m_peers[sender];              // operator[] создаст пустого
    p.wanted = wanted;
    // Плитка родилась. Если декодер жив и поток за время пересадки не рвался —
    // рисуем дальше молча: ни чёрного кадра, ни лишнего KEYFRAME_REQ. Иначе
    // просим опорный кадр сразу, а не ждём общей каденции до трёх секунд.
    if (wanted && (!p.dec || p.awaitKey)) {
        setAwaitKey(p, sender, true);
        emit keyframeNeeded(m_codedType);
    }
}

void VideoRecvWorker::setAwaitKey(Peer& p, quint32 sender, bool awaiting) {
    if (p.awaitKey == awaiting) return;
    p.awaitKey = awaiting;
    emit awaitKeyChanged(sender, awaiting);
}

// Пускать ли кадр в очередь. Зовётся с потока транспорта — см. объявление.
bool VideoRecvWorker::offer(quint32* generation) {
    if (generation) *generation = m_generation.load(std::memory_order_acquire);
    if (m_buffered.load(std::memory_order_relaxed)) {
        m_inFlight.fetch_add(1, std::memory_order_release);
        return true;
    }
    // Буфер выключен: место ровно на один кадр. Если предыдущий ещё не
    // разобран, этот не встаёт в очередь, а выбрасывается здесь же — то есть
    // отставание не может накопиться в принципе. Расплата известна: декодер
    // теряет кусок потока и попросит опорный кадр, а это секунда заглушки
    // вместо минуты растущего опоздания.
    int expected = 0;
    if (!m_inFlight.compare_exchange_strong(expected, 1,
                                            std::memory_order_acq_rel,
                                            std::memory_order_relaxed)) {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        // Отметить дыру в потоке обязательно. Декодер о выброшенном кадре не
        // узнает никак, а следующая дельта опирается на то, чего он не видел:
        // получилась бы рассыпающаяся картинка вместо честной паузы до
        // опорного кадра. Флаг снимет сам воркер, на своём потоке.
        m_gap.store(true, std::memory_order_release);
        return false;
    }
    return true;
}

// Очередь была выброшена (сбросом или переполнением без буфера): декодеры
// стоят на середине потока, и дельты им теперь не годятся.
void VideoRecvWorker::restartAfterFlush() {
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it)
        if (it->wanted) setAwaitKey(*it, it.key(), true);
    emit keyframeNeeded(m_codedType);
}

void VideoRecvWorker::onFrame(const QByteArray& d, quint32 generation) {
    // Счётчик отпускаем в любом случае и первым делом: что бы дальше ни
    // случилось, место для следующего кадра должно освободиться.
    struct Release {
        std::atomic<int>* n;
        ~Release() { n->fetch_sub(1, std::memory_order_release); }
    } release{ &m_inFlight };

    // Кадр из прошлой жизни: между постановкой в очередь и этим моментом
    // сработал сброс. Декодировать его незачем — ради этого сброс и затевался.
    const quint32 now = m_generation.load(std::memory_order_acquire);
    if (generation != now) return;

    // Первый кадр после сброса — здесь же и чиним поток: декодеры остались на
    // середине выброшенного куска, и дельты им теперь не годятся. Делать это
    // в самом flush() нельзя, он зовётся с чужого потока.
    if (m_seenGeneration != now) {
        m_seenGeneration = now;
        m_gap.store(false, std::memory_order_relaxed);
        restartAfterFlush();
    } else if (m_gap.exchange(false, std::memory_order_acq_rel)) {
        // Кадр (или несколько) выброшен в offer из-за выключенного буфера.
        restartAfterFlush();
    }
    onFrameBody(d);
}

void VideoRecvWorker::onFrameBody(const QByteArray& d) {
    // Транспорт вещает обе полосы обоим воркерам, и чужое надо отбросить ДО
    // разбора: unpack копирует payload себе, а кадр демонстрации 4K — это
    // сотни килобайт, которые второй воркер скопировал бы только чтобы
    // выбросить. Тип лежит в первом байте (§5.3).
    const quint8 type = d.isEmpty() ? 0 : quint8(d[0]);
    if (type != m_codedType && type != m_jpegType) return;

    Proto::FrameV2 f;
    if (!Proto::unpack(d, f)) return;              // мусор короче заголовка

    if (f.type == m_jpegType) {
        Peer& p = m_peers[f.sender];
        if ((f.flags & Proto::FLAG_ENCRYPTED)
            && !unseal(p, f.sender, f.type, f.codec, f.payload)) return;
        emitJpeg(f.sender, f.payload, qint64(f.ts));
        return;
    }
    if (f.type != m_codedType) return;
    if (f.codec != Proto::CODEC_H264 && f.codec != Proto::CODEC_VP8 &&
        f.codec != Proto::CODEC_VP9 && f.codec != Proto::CODEC_HEVC &&
        f.codec != Proto::CODEC_AV1) {
        // Раньше такой кадр отбрасывался молча, и это было худшим из
        // возможных поведений: у нас пусто, а отправитель уверен, что всё
        // хорошо. Теперь жалуемся — движок передаст жалобу ему, и он вернётся
        // на кодек, который понимают все.
        complain(f.codec);
        return;
    }

    // Расшифровываем ДО декодера: он должен получить те же байты, что вышли из
    // кодера отправителя, иначе поток для него — мусор.
    Peer& p = m_peers[f.sender];
    if ((f.flags & Proto::FLAG_ENCRYPTED)
        && !unseal(p, f.sender, f.type, f.codec, f.payload)) return;

    routeCoded(f.sender, f.flags, f.codec, f.ts, f.payload);
}

// Кадр не открылся — у собеседника другой ключ или его нет. Один такой кадр
// ещё ничего не значит (мог прилететь хвост со старым ключом), а три подряд —
// уже состояние, и о нём нужно сказать человеку.
bool VideoRecvWorker::unseal(Peer& p, quint32 sender, quint8 type, quint8 codec,
                             QByteArray& payload) {
    const QByteArray plain = m_cipher->open(type, codec, payload);
    if (plain.isEmpty()) {
        if (++p.cryptoFails >= 3) {
            setLocked(p, sender, true);
            setAwaitKey(p, sender, true);   // поток для нас прерван
        }
        return false;
    }
    p.cryptoFails = 0;
    setLocked(p, sender, false);
    payload = plain;
    return true;
}

void VideoRecvWorker::setLocked(Peer& p, quint32 sender, bool locked) {
    if (p.locked == locked) return;
    p.locked = locked;
    emit peerLocked(qint64(sender), locked);
}

void VideoRecvWorker::complain(quint8 codec) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastCodecComplaintMs <= 2000) return;
    m_lastCodecComplaintMs = now;
    emit codecUnsupported(codec);
}

// «Понимаю, но не тяну» — жалоба той же дорогой, что и «не понимаю вовсе».
//
// Разница между этими случаями есть, а вот последствие у них одно: зритель
// остаётся без картинки. Программный разбор HEVC 1080p60 стоит 13 мс среднего
// при выбросах под 108 — в кадровый бюджет 16.7 мс это не влезает, очередь
// кадров начинает расти, и отставание уже не отдать: оно живёт до конца
// потока. Отправитель по этой жалобе спустится на ступень (HEVC -> H.264 у
// экрана, VP9 -> H.264 у камеры), а H.264 разбирает железом почти любая
// машина — то есть лечится ровно то, что сломано.
//
// Жалуемся ТОЛЬКО про кодеки, ниже которых на лестнице есть куда идти.
// Пожаловаться на H.264 или VP8 значило бы попросить отправителя перейти на
// VP8, который ещё тяжелее, — то есть сделать хуже.
void VideoRecvWorker::checkPace(Peer& p, quint8 codec, qint64 decUs, qint64 now) {
    // Видеокарта разбирает — вопроса нет; ей эти кадры стоят доли миллисекунды.
    if (p.complainedSlow || !p.dec || p.dec->isHardware()) return;
    if (codec != Proto::CODEC_HEVC && codec != Proto::CODEC_AV1
        && codec != Proto::CODEC_VP9) return;

    p.decEmaUs = p.decEmaUs > 0 ? p.decEmaUs * 0.9 + double(decUs) * 0.1
                                : double(decUs);
    if (p.prevAt) {
        const qint64 ivl = now - p.prevAt;
        // Пауза длиннее секунды — это не «медленно», а «кадров не было»
        // (статичный экран, свёрнутое окно): в среднее её брать нельзя.
        if (ivl > 0 && ivl < 1000)
            p.ivlEmaMs = p.ivlEmaMs > 0 ? p.ivlEmaMs * 0.9 + double(ivl) * 0.1
                                        : double(ivl);
    }
    p.prevAt = now;
    if (p.ivlEmaMs <= 0) return;

    // Порог в 70 % интервала, а не 100: впритык — это уже поздно. Кадры
    // приходят неровно, и полоса, съедающая семь десятых бюджета в среднем,
    // на всплесках гарантированно копит очередь.
    if (p.decEmaUs > p.ivlEmaMs * 1000.0 * 0.7) ++p.slowRun;
    else                                        p.slowRun = 0;
    if (p.slowRun < 90) return;                  // полторы секунды на 60 к/с

    p.complainedSlow = true;
    qWarning() << "VideoRecvWorker: кодек" << codec << "разбирается процессором"
               << int(p.decEmaUs / 1000) << "мс при интервале"
               << int(p.ivlEmaMs) << "мс — просим отправителя спуститься";
    complain(codec);
}

void VideoRecvWorker::routeCoded(quint32 sender, quint8 flags, quint8 codec,
                                 quint64 ts, const QByteArray& payload) {
    Peer& p = m_peers[sender];
    if (!p.wanted) {
        // Рисовать некуда — не тратим процессор на декод в никуда. Но
        // пропущенные кадры выбивают декодер из потока, поэтому следующей
        // плитке нужен опорный: без этой пометки setWanted принял бы декодер
        // за годный.
        setAwaitKey(p, sender, true);
        return;
    }

    // Декодер: создать под первый кадр; пересоздать, если отправитель сменил
    // кодек (правило §5.4 — у веба это смена браузера после реконнекта).
    if (!p.dec || p.dec->codec() != codec) {
        dropDecoder(p);
        p.dec = new VideoDecoder;
        if (!p.dec->open(codec)) {
            // Кодек из списка известных, а декодера под него в сборке FFmpeg
            // не оказалось. Раньше здесь стоял молчаливый выход — то есть тот
            // самый худший случай, ради которого и заводили CODEC_UNSUPPORTED:
            // у нас пусто, а отправитель уверен, что всё хорошо.
            delete p.dec;
            p.dec = nullptr;
            complain(codec);
            return;
        }
        setAwaitKey(p, sender, true);
    }

    // Дельта-кадры до опорного — мусор: молча дропаем и просим keyframe.
    const bool isKey = (flags & Proto::FLAG_KEYFRAME) != 0;
    if (p.awaitKey && !isKey) { emit keyframeNeeded(m_codedType); return; }
    setAwaitKey(p, sender, false);

    QElapsedTimer clock;
    clock.start();
    const AVFrame* frame = p.dec->decode(payload, ts);

    // Декодер сломался (битый поток?) — правило §5.4: пересоздать и ждать
    // keyframe заново. Пересоздание случится само на следующем кадре.
    if (p.dec->failed()) {
        dropDecoder(p);
        setAwaitKey(p, sender, true);
        emit keyframeNeeded(m_codedType);
        return;
    }
    if (!frame) return;

    const QVideoFrame vf = convert(p, frame);
    // Приведение пикселей считаем частью разбора: для зрителя это одна работа,
    // и разносить её по двум числам значило бы прятать половину цены кадра.
    const qint64 decUs = clock.nsecsElapsed() / 1000;
    if (!vf.isValid()) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    p.lastAt = now;
    // Аппаратность известна только после первого разобранного кадра — вот
    // здесь и говорим, чем на самом деле разбираем. И повторяем всякий раз,
    // когда ответ меняется: ведущий волен переключить разрешение посреди
    // демонстрации, а решение видеокарты взяться за поток зависит в том числе
    // от размера кадра.
    const bool hw = p.dec->isHardware();
    if (p.annW != frame->width || p.annH != frame->height || p.annHw != hw) {
        p.annW = frame->width;
        p.annH = frame->height;
        p.annHw = hw;
        emit decoderOpened(codec, frame->width, frame->height, hw);
    }
    emit frameDecoded(decUs);
    checkPace(p, codec, decUs, now);
    emit frameReady(sender, vf, qint64(ts));
}

// AVFrame → QVideoFrame. sws_scale пишет прямо в плоскости QVideoFrame: для
// YUV420P это быстрое копирование с учётом stride, для экзотики (10-битный VP9
// и т.п.) — честная конверсия. YUV→RGB на экране делает GPU при отрисовке —
// CPU в цвета не лезет.
QVideoFrame VideoRecvWorker::convert(Peer& p, const AVFrame* f) {
    // Кадр от видеокарты приходит в NV12, и QVideoFrame его понимает как есть.
    // Гнать его через sws в YUV420P значило бы добавить полный проход по кадру
    // (на 4К это 12 МБ) ровно затем, чтобы переложить цветность из одной
    // плоскости в две, — а рисует их всё равно видеокарта при отрисовке.
    if (f->format == AV_PIX_FMT_NV12) {
        QVideoFrame vf(QVideoFrameFormat(QSize(f->width, f->height),
                                         QVideoFrameFormat::Format_NV12));
        if (!vf.map(QVideoFrame::WriteOnly)) return {};
        if (vf.planeCount() >= 2) {
            for (int y = 0; y < f->height; ++y)
                memcpy(vf.bits(0) + qsizetype(y) * vf.bytesPerLine(0),
                       f->data[0] + qsizetype(y) * f->linesize[0],
                       size_t(qMin(vf.bytesPerLine(0), f->linesize[0])));
            for (int y = 0; y < f->height / 2; ++y)
                memcpy(vf.bits(1) + qsizetype(y) * vf.bytesPerLine(1),
                       f->data[1] + qsizetype(y) * f->linesize[1],
                       size_t(qMin(vf.bytesPerLine(1), f->linesize[1])));
            vf.unmap();
            return vf;
        }
        vf.unmap();
        return {};
    }

    QVideoFrameFormat fmt(QSize(f->width, f->height),
                          QVideoFrameFormat::Format_YUV420P);
    QVideoFrame vf(fmt);
    if (!vf.map(QVideoFrame::WriteOnly)) return {};

    p.sws = sws_getCachedContext(p.sws,
        f->width, f->height, AVPixelFormat(f->format),
        f->width, f->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!p.sws) { vf.unmap(); return {}; }

    uint8_t* dst[4]       = { vf.bits(0), vf.bits(1), vf.bits(2), nullptr };
    int      dstStride[4] = { vf.bytesPerLine(0), vf.bytesPerLine(1),
                              vf.bytesPerLine(2), 0 };
    sws_scale(p.sws, f->data, f->linesize, 0, f->height, dst, dstStride);

    vf.unmap();
    return vf;
}

// Legacy-камера: целый JPEG в payload (≤480×360, ~10 к/с). Декодер не нужен —
// JPEG умеет Qt, а QVideoFrame с Qt 6.8 строится прямо из QImage.
void VideoRecvWorker::emitJpeg(quint32 sender, const QByteArray& jpeg, qint64 tsMs) {
    Peer& p = m_peers[sender];
    if (!p.wanted) return;
    const QImage img = QImage::fromData(jpeg, "JPEG");
    if (img.isNull()) return;
    p.lastAt = QDateTime::currentMSecsSinceEpoch();
    emit frameReady(sender, QVideoFrame(img), tsMs);
}
