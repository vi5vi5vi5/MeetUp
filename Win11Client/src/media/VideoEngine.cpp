#include "VideoEngine.h"
#include "VideoDecoder.h"
#include "VideoSendWorker.h"
#include "../MediaSettings.h"
#include "../net/SignalingClient.h"
#include "../net/Protocol.h"
#include <QVideoSink>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QMediaCaptureSession>
#include <QThread>
#include <QDateTime>
#include <QImage>
#include <QTimer>
#include <QDebug>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

// Затор в сокете, после которого видеокадры пропускаются (§5.5).
static const qint64 kMaxBuffered = 1500000;

VideoEngine::VideoEngine(SignalingClient* conf, MediaSettings* settings, QObject* parent)
    : QObject(parent), m_conf(conf), m_settings(settings)
{
    connect(conf, &SignalingClient::binaryFrame,       this, &VideoEngine::onBinaryFrame);
    connect(conf, &SignalingClient::joinOk,            this, &VideoEngine::onJoinOk);
    connect(conf, &SignalingClient::participantLeft,   this, &VideoEngine::onParticipantLeft);
    connect(conf, &SignalingClient::left,              this, &VideoEngine::onLeft);

    // Отправка: захват идёт, пока мы в эфире и камера включена.
    connect(conf, &SignalingClient::phaseChanged,      this, &VideoEngine::onPhase);
    connect(conf, &SignalingClient::localStateChanged, this, &VideoEngine::onLocalState);
    // Новичок в комнате не должен ждать опорный кадр до 3 с (§4.2).
    connect(conf, &SignalingClient::participantJoined, this, [this](qint64) { forceKeyframe(); });
    // Смена камеры или пресета качества — перезапуск захвата на лету.
    connect(settings, &MediaSettings::camIdChanged,      this, &VideoEngine::restartCapture);
    connect(settings, &MediaSettings::camQualityChanged, this, &VideoEngine::restartCapture);

    // Кодирующий поток: тяжёлые sws_scale + encode уходят с GUI-потока, чтобы
    // окно не дёргалось при перетаскивании во время захвата.
    m_encThread = new QThread(this);
    m_encThread->setObjectName("video-encode");
    m_worker = new VideoSendWorker;            // без parent — живёт на своём потоке
    m_worker->moveToThread(m_encThread);
    connect(m_encThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &VideoEngine::frameToEncode, m_worker, &VideoSendWorker::encode);
    // Готовый пакет возвращается на GUI-поток и уходит в сокет (queued).
    connect(m_worker, &VideoSendWorker::packetReady, this,
            [this](const QByteArray& frame) { m_conf->sendBinary(frame); });
    m_encThread->start();

    // Сторож замёрзших плиток: раз в секунду проверяем, у кого кадры иссякли.
    // Работает всегда — вне конференции m_peers пуст, обход бесплатен.
    m_staleTimer = new QTimer(this);
    m_staleTimer->setInterval(1000);
    connect(m_staleTimer, &QTimer::timeout, this, &VideoEngine::sweepStale);
    m_staleTimer->start();
}

VideoEngine::~VideoEngine() {
    stopCapture();
    if (m_encThread) {                         // остановить поток кодирования
        m_encThread->quit();
        m_encThread->wait();                   // finished -> deleteLater воркера
    }
    for (Peer& p : m_peers) {
        delete p.dec;
        if (p.sws) sws_freeContext(p.sws);
    }
    m_peers.clear();
}

// ---------- связь с плитками ----------

void VideoEngine::attach(qint64 id, QVideoSink* sink) {
    Peer& p = m_peers[quint32(id)];    // operator[] создаст пустого
    p.sink = sink;
    p.awaitKey = true;                 // новой плитке нужен свежий опорный кадр
    requestKeyframe();                 // …и мы его просим, а не ждём до 3 с
}

void VideoEngine::detach(qint64 id, QVideoSink* sink) {
    auto it = m_peers.find(quint32(id));
    if (it == m_peers.end()) return;
    if (sink && it->sink != sink) return;   // привязку уже перехватила новая плитка
    it->sink = nullptr;
    delete it->dec;                    // декодер без плитки бессмыслен:
    it->dec = nullptr;                 // кадры мы всё равно дропаем (см. ниже)
    if (it->sws) { sws_freeContext(it->sws); it->sws = nullptr; }
    it->awaitKey = true;
    it->active = false;
}

void VideoEngine::attachPreview(QVideoSink* sink) { m_preview = sink; }

void VideoEngine::detachPreview() { m_preview = nullptr; }

// ---------- жизненный цикл ----------

// Мягкий сброс: всё декодированное состояние — в мусор, а вот привязки
// плиток НЕ трогаем. Repeater пересоздаёт плитки сам (модель участников
// меняется), и гонка «кто раньше — наш сброс или attach новой плитки»
// нам не грозит: sink переживает сброс.
void VideoEngine::resetPeers() {
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        Peer& p = it.value();
        delete p.dec;
        p.dec = nullptr;
        if (p.sws) { sws_freeContext(p.sws); p.sws = nullptr; }
        p.awaitKey = true;
        if (p.active) {
            p.active = false;
            emit videoChanged(qint64(it.key()), false);
        }
    }
}

// Каждый join_ok — в т.ч. РЕКОННЕКТ: декодеры отстали от потока навсегда,
// а у анонимов ещё и id новые. Свежие keyframe попросят attach'и плиток.
// Пирам после нашего обрыва тоже нужен опорный кадр — шлём без просьбы.
void VideoEngine::onJoinOk() {
    resetPeers();
    m_keyNext = true;
}

void VideoEngine::onLeft() {
    resetPeers();
    m_live = false;                    // фаза осталась "live", но комнаты уже нет
    updateCapture();
}

void VideoEngine::onParticipantLeft(qint64 id) {
    auto it = m_peers.find(quint32(id));
    if (it == m_peers.end()) return;
    if (it->active) emit videoChanged(id, false);
    delete it->dec;
    if (it->sws) sws_freeContext(it->sws);
    m_peers.erase(it);
}

// Сторож: вещатель умер молча (вкладку убили, ноутбук уснул) — через 5 с
// прячем замороженный кадр, плитка возвращается к аватару (правило §5.4).
void VideoEngine::sweepStale() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        Peer& p = it.value();
        if (!p.active || now - p.lastFrameAt <= 5000) continue;
        p.active = false;
        p.awaitKey = true;             // поток вернётся — начнём с опорного
        emit videoChanged(qint64(it.key()), false);
    }
}

// ---------- приём ----------

void VideoEngine::requestKeyframe() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastKeyReqAt < 1000) return;   // rate-limit как у веба
    m_lastKeyReqAt = now;
    // Пустой payload, codec 0, ts 0 (§5.3). Сервер разошлёт всем вещающим.
    m_conf->sendBinary(Proto::pack(Proto::KEYFRAME_REQ, 0, 0, 0, QByteArray()));
}

void VideoEngine::onBinaryFrame(const QByteArray& d) {
    Proto::FrameV2 f;
    if (!Proto::unpack(d, f)) return;

    if (f.type == Proto::KEYFRAME_REQ) {           // кто-то просит опорный кадр
        forceKeyframe();
        return;
    }
    if (f.type == Proto::VIDEO_JPEG) {             // legacy-браузеры без WebCodecs
        if (f.flags & Proto::FLAG_ENCRYPTED) return;
        paintJpeg(m_peers[f.sender], f.sender, f.payload);
        return;
    }
    if (f.type != Proto::VIDEO_CODED) return;      // экран — M7, звук — не наш
    if (f.flags & Proto::FLAG_ENCRYPTED) return;   // E2E — M5
    if (f.codec != Proto::CODEC_H264 && f.codec != Proto::CODEC_VP8 &&
        f.codec != Proto::CODEC_VP9) return;       // незнакомое — молча мимо

    Peer& p = m_peers[f.sender];
    if (!p.sink) return;   // плитки (ещё) нет — не тратим CPU на декод в никуда

    // Декодер: создать под первый кадр; пересоздать, если отправитель сменил
    // кодек (правило §5.4 — у веба это смена браузера после реконнекта).
    if (!p.dec || p.dec->codec() != f.codec) {
        delete p.dec;
        p.dec = new VideoDecoder;
        if (!p.dec->open(f.codec)) { delete p.dec; p.dec = nullptr; return; }
        p.awaitKey = true;
    }

    // Дельта-кадры до опорного — мусор: молча дропаем и просим keyframe.
    const bool isKey = (f.flags & Proto::FLAG_KEYFRAME) != 0;
    if (p.awaitKey && !isKey) { requestKeyframe(); return; }
    p.awaitKey = false;

    const AVFrame* frame = p.dec->decode(f.payload, f.ts);

    // Декодер сломался (битый поток?) — правило §5.4: пересоздать и ждать
    // keyframe заново. Пересоздание случится само на следующем кадре.
    if (p.dec->failed()) {
        delete p.dec;
        p.dec = nullptr;
        p.awaitKey = true;
        requestKeyframe();
        return;
    }

    if (frame) deliver(p, f.sender, frame);
}

// AVFrame → QVideoFrame → плитка. sws_scale пишет прямо в плоскости
// QVideoFrame: для YUV420P это быстрое копирование с учётом stride, для
// экзотики (10-битный VP9 и т.п.) — честная конверсия. YUV→RGB на экране
// делает GPU при отрисовке — CPU в цвета не лезет.
void VideoEngine::deliver(Peer& p, quint32 sender, const AVFrame* f) {
    QVideoFrameFormat fmt(QSize(f->width, f->height),
                          QVideoFrameFormat::Format_YUV420P);
    QVideoFrame vf(fmt);
    if (!vf.map(QVideoFrame::WriteOnly)) return;

    p.sws = sws_getCachedContext(p.sws,
        f->width, f->height, AVPixelFormat(f->format),
        f->width, f->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!p.sws) { vf.unmap(); return; }

    uint8_t* dst[4]       = { vf.bits(0), vf.bits(1), vf.bits(2), nullptr };
    int      dstStride[4] = { vf.bytesPerLine(0), vf.bytesPerLine(1),
                              vf.bytesPerLine(2), 0 };
    sws_scale(p.sws, f->data, f->linesize, 0, f->height, dst, dstStride);

    vf.unmap();
    p.sink->setVideoFrame(vf);

    p.lastFrameAt = QDateTime::currentMSecsSinceEpoch();
    if (!p.active) {
        p.active = true;
        emit videoChanged(qint64(sender), true);  // плитка: «показывай видео»
    }
}

// Legacy-камера: целый JPEG в payload (≤480×360, ~10 к/с). Декодер не нужен —
// JPEG умеет Qt, а QVideoFrame с Qt 6.8 строится прямо из QImage.
void VideoEngine::paintJpeg(Peer& p, quint32 sender, const QByteArray& jpeg) {
    if (!p.sink) return;
    const QImage img = QImage::fromData(jpeg, "JPEG");
    if (img.isNull()) return;

    p.sink->setVideoFrame(QVideoFrame(img));

    p.lastFrameAt = QDateTime::currentMSecsSinceEpoch();
    if (!p.active) { p.active = true; emit videoChanged(qint64(sender), true); }
}

// ---------- отправка ----------

void VideoEngine::onPhase() {
    m_live = (m_conf->phase() == "live");
    updateCapture();
}

void VideoEngine::onLocalState(bool /*mic*/, bool cam) {
    m_camOn = cam;
    updateCapture();
}

// Захват идёт <=> мы в эфире И камера включена. Все дороги ведут сюда.
void VideoEngine::updateCapture() {
    const bool want = m_live && m_camOn;
    if (want && !m_camera)       startCapture();
    else if (!want && m_camera)  stopCapture();
}

void VideoEngine::restartCapture() {
    if (!m_camera) return;         // не снимаем — новые настройки подхватит старт
    stopCapture();
    updateCapture();
}

void VideoEngine::startCapture() {
    const QCameraDevice dev = m_settings->camera();
    if (dev.isNull()) { qWarning() << "VideoEngine: камера не найдена"; return; }

    const MediaSettings::CamPreset q = m_settings->camPreset();

    // Формат камеры: ближайший к пресету по площади кадра; формат, не дотянувший
    // по fps, штрафуется — пусть лучше будет чуть крупнее, но плавно.
    QCameraFormat best;
    qint64 bestScore = -1;
    for (const QCameraFormat& f : dev.videoFormats()) {
        const QSize r = f.resolution();
        qint64 score = qAbs(qint64(r.width()) * r.height() - qint64(q.width) * q.height);
        if (f.maxFrameRate() > 0 && f.maxFrameRate() < q.fps) score += 4000000;
        if (bestScore < 0 || score < bestScore) { bestScore = score; best = f; }
    }

    m_camera = new QCamera(dev, this);
    if (!best.isNull()) m_camera->setCameraFormat(best);
    m_session = new QMediaCaptureSession(this);
    m_capSink = new QVideoSink(this);
    m_session->setCamera(m_camera);
    m_session->setVideoSink(m_capSink);
    connect(m_capSink, &QVideoSink::videoFrameChanged, this, &VideoEngine::onCamFrame);
    connect(m_camera, &QCamera::errorOccurred, this,
            [](QCamera::Error e, const QString& s) {
                if (e != QCamera::NoError) qWarning() << "VideoEngine: камера:" << s;
            });

    m_keyNext = true;              // первый кадр нового захвата — опорный
    m_lastEncodeAt = 0;
    m_camera->start();
}

void VideoEngine::stopCapture() {
    if (m_camera) {
        m_camera->stop();
        m_camera->deleteLater();  m_camera = nullptr;
        m_session->deleteLater(); m_session = nullptr;
        m_capSink->deleteLater(); m_capSink = nullptr;
    }
    // Энкодер и sws живут на кодирующем потоке — сброс тоже там (queued).
    if (m_worker) QMetaObject::invokeMethod(m_worker, "reset", Qt::QueuedConnection);
    m_keyNext = true;
    if (m_preview) m_preview->setVideoFrame(QVideoFrame());   // стереть стоп-кадр
    setPreviewActive(false);
}

void VideoEngine::setPreviewActive(bool on) {
    if (m_previewActive == on) return;
    m_previewActive = on;
    emit previewActiveChanged();
}

// Просьба прислать опорный кадр (KEYFRAME_REQ, новичок, реконнект).
// Rate-limit 500 мс — как у веба: спам запросов не роняет битрейт.
void VideoEngine::forceKeyframe() {
    if (!m_camera) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastForceAt < 500) return;
    m_lastForceAt = now;
    m_keyNext = true;
}

void VideoEngine::onCamFrame(const QVideoFrame& frame) {
    if (!frame.isValid()) return;

    // Превью в self-плитку — всегда и прямо здесь (на GUI/рендер-потоке): своё
    // лицо живёт без задержек и не зависит от заторов; это дёшево.
    if (m_preview) m_preview->setVideoFrame(frame);
    setPreviewActive(true);

    if (!m_live) return;
    const MediaSettings::CamPreset q = m_settings->camPreset();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastEncodeAt < 1000 / q.fps) return;         // троттлинг к fps пресета
    if (m_conf->bufferedBytes() > kMaxBuffered) return;      // затор — кадр в мусор (§5.5)
    m_lastEncodeAt = now;

    // Тяжёлый sws+encode — на кодирующем потоке. QVideoFrame неявно расшарен:
    // копия дешёвая, буфер общий; воркер маппит его read-only у себя.
    const bool forceKey = m_keyNext;
    m_keyNext = false;
    emit frameToEncode(frame, q.width, q.height, q.fps, q.bitrate, forceKey, now);
}
