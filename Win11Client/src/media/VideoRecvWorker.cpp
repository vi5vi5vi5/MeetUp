#include "VideoRecvWorker.h"
#include "VideoDecoder.h"
#include "../net/Protocol.h"
#include "../crypto/E2eCipher.h"
#include <QVideoFrameFormat>
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
        emit keyframeNeeded();
    }
}

void VideoRecvWorker::setAwaitKey(Peer& p, quint32 sender, bool awaiting) {
    if (p.awaitKey == awaiting) return;
    p.awaitKey = awaiting;
    emit awaitKeyChanged(sender, awaiting);
}

void VideoRecvWorker::onFrame(const QByteArray& d) {
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
        // на кодек, который понимают все. Не чаще раза в две секунды: кадры
        // идут потоком, а сообщение нужно одно.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastCodecComplaintMs > 2000) {
            m_lastCodecComplaintMs = now;
            emit codecUnsupported(f.codec);
        }
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
        if (!p.dec->open(codec)) { delete p.dec; p.dec = nullptr; return; }
        setAwaitKey(p, sender, true);
    }

    // Дельта-кадры до опорного — мусор: молча дропаем и просим keyframe.
    const bool isKey = (flags & Proto::FLAG_KEYFRAME) != 0;
    if (p.awaitKey && !isKey) { emit keyframeNeeded(); return; }
    setAwaitKey(p, sender, false);

    const AVFrame* frame = p.dec->decode(payload, ts);

    // Декодер сломался (битый поток?) — правило §5.4: пересоздать и ждать
    // keyframe заново. Пересоздание случится само на следующем кадре.
    if (p.dec->failed()) {
        dropDecoder(p);
        setAwaitKey(p, sender, true);
        emit keyframeNeeded();
        return;
    }
    if (!frame) return;

    const QVideoFrame vf = convert(p, frame);
    if (!vf.isValid()) return;
    p.lastAt = QDateTime::currentMSecsSinceEpoch();
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
