#include "VideoEngine.h"
#include "VideoDecoder.h"
#include "VideoSendWorker.h"
#include "ScreenCursor.h"
#include "AudioEngine.h"
#include "../MediaSettings.h"
#include "../ScreenSources.h"
#include "../net/SignalingClient.h"
#include "../net/Protocol.h"
#include <QVideoSink>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QScreenCapture>
#include <QWindowCapture>
#include <QScreen>
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
// Для экрана порог НАМНОГО ниже: лицо к пропаже кадра чувствительнее, чем
// слайд, а кадры экрана — самые тяжёлые в сокете. Роняя их первыми, мы
// освобождаем очередь для голоса и камеры, и губы не разъезжаются.
static const qint64 kMaxBufferedScreen = 350000;

VideoEngine::VideoEngine(SignalingClient* conf, MediaSettings* settings,
                         ScreenSources* sources, AudioEngine* audio, QObject* parent)
    : QObject(parent), m_conf(conf), m_settings(settings), m_sources(sources),
      m_audio(audio)
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
    // Разрешение и частота демонстрации: битрейт зашит в открытый энкодер,
    // поэтому мало поменять пресет — полосу надо перезапустить.
    connect(settings, &MediaSettings::screenResChanged,
            this, &VideoEngine::restartScreenCapture);
    connect(settings, &MediaSettings::screenFpsChanged,
            this, &VideoEngine::restartScreenCapture);
    // Слот демонстрации закрепил сервер (§4.3) — только после этого снимаем.
    connect(conf, &SignalingClient::screenChanged, this, &VideoEngine::onScreenSlotChanged);

    // Кодирование живёт вне GUI-потока: тяжёлые sws_scale + encode на нём
    // запинали бы отрисовку (при перетаскивании окна — особенно заметно).
    // Потоков ДВА, по одному на полосу: у каждой свой энкодер (он держит
    // состояние потока кадров), и, главное, кадр камеры не должен стоять в
    // очереди за кадром экрана — иначе он приезжает получателю просроченным.
    m_encThread = new QThread(this);
    m_encThread->setObjectName("video-encode");
    m_scrThread = new QThread(this);
    m_scrThread->setObjectName("screen-encode");
    m_worker = new VideoSendWorker(Proto::VIDEO_CODED);   // без parent: свой поток
    m_scrWorker = new VideoSendWorker(Proto::SCREEN_CODED);
    m_worker->moveToThread(m_encThread);
    m_scrWorker->moveToThread(m_scrThread);
    connect(m_encThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_scrThread, &QThread::finished, m_scrWorker, &QObject::deleteLater);
    connect(this, &VideoEngine::frameToEncode, m_worker, &VideoSendWorker::encode);
    connect(this, &VideoEngine::screenFrameToEncode, m_scrWorker, &VideoSendWorker::encode);
    // Готовый пакет возвращается на GUI-поток и уходит в сокет (queued).
    connect(m_worker, &VideoSendWorker::packetReady, this,
            [this](const QByteArray& frame) { m_conf->sendBinary(frame); });
    connect(m_scrWorker, &VideoSendWorker::packetReady, this,
            [this](const QByteArray& frame) { m_conf->sendBinary(frame); });
    // Учёт занятости воркеров: пока кадр не отработан, следующий не шлём.
    connect(m_worker, &VideoSendWorker::frameDone, this,
            [this] { if (m_encInFlight > 0) --m_encInFlight; });
    connect(m_scrWorker, &VideoSendWorker::frameDone, this,
            [this] { if (m_scrInFlight > 0) --m_scrInFlight; });
    m_encThread->start();
    m_scrThread->start();

    // Сторож замёрзших плиток: раз в секунду проверяем, у кого кадры иссякли.
    // Работает всегда — вне конференции m_peers пуст, обход бесплатен.
    m_staleTimer = new QTimer(this);
    m_staleTimer->setInterval(1000);
    connect(m_staleTimer, &QTimer::timeout, this, &VideoEngine::sweepStale);
    m_staleTimer->start();

    // Отложенная просьба об опорном кадре (см. requestKeyframe).
    m_keyReqTimer = new QTimer(this);
    m_keyReqTimer->setSingleShot(true);
    connect(m_keyReqTimer, &QTimer::timeout, this, &VideoEngine::requestKeyframe);

    // Насос придержанных кадров (синхронизация губ). Работает только когда
    // есть что придерживать — в тишине не тикает.
    m_holdTimer = new QTimer(this);
    m_holdTimer->setInterval(15);
    connect(m_holdTimer, &QTimer::timeout, this, &VideoEngine::drainHeld);
}

VideoEngine::~VideoEngine() {
    stopCapture();
    stopScreenCapture();
    if (m_encThread) {                         // остановить потоки кодирования
        m_encThread->quit();
        m_encThread->wait();                   // finished -> deleteLater воркера
    }
    if (m_scrThread) {
        m_scrThread->quit();
        m_scrThread->wait();
    }
    for (QHash<quint32, Peer>* h : { &m_peers, &m_screenPeers }) {
        for (Peer& p : *h) {
            delete p.dec;
            if (p.sws) sws_freeContext(p.sws);
        }
        h->clear();
    }
}

// ---------- связь с плитками ----------

int VideoEngine::attach(qint64 id, QVideoSink* sink) {
    Peer& p = m_peers[quint32(id)];    // operator[] создаст пустого
    p.sink = sink;
    p.token = ++m_attachSeq;
    p.awaitKey = true;                 // новой плитке нужен свежий опорный кадр
    // Сбросить «картинка идёт» ОБЯЗАТЕЛЬНО: плитка рождается с live=false и
    // ждёт сигнала videoChanged(true), а он приходит только на переходе
    // false->true. Иначе плитка, заменившая другую (сетка <-> сцена), сидела
    // бы на аватарке при живом потоке кадров.
    p.active = false;
    requestKeyframe();                 // …и мы просим кадр, а не ждём до 3 с
    return p.token;
}

void VideoEngine::detach(qint64 id, int token) {
    auto it = m_peers.find(quint32(id));
    if (it == m_peers.end()) return;
    if (token != it->token) return;    // привязку уже перехватила новая плитка
    it->sink = nullptr;
    it->token = 0;
    delete it->dec;                    // декодер без плитки бессмыслен:
    it->dec = nullptr;                 // кадры мы всё равно дропаем (см. ниже)
    if (it->sws) { sws_freeContext(it->sws); it->sws = nullptr; }
    it->awaitKey = true;
    it->active = false;
    it->holdQ.clear();                 // рисовать больше некуда
}

int VideoEngine::attachPreview(QVideoSink* sink) {
    m_preview = sink;
    m_previewToken = ++m_attachSeq;
    return m_previewToken;
}

void VideoEngine::detachPreview(int token) {
    if (token != m_previewToken) return;   // превью уже перехватила новая плитка
    m_preview = nullptr;
    m_previewToken = 0;
}

// Сцена демонстрации — та же механика, только полоса другая.
int VideoEngine::attachScreen(qint64 id, QVideoSink* sink) {
    Peer& p = m_screenPeers[quint32(id)];
    p.sink = sink;
    p.token = ++m_attachSeq;
    p.awaitKey = true;
    p.active = false;                  // см. attach(): сигнал даёт только переход
    requestKeyframe();
    return p.token;
}

void VideoEngine::detachScreen(qint64 id, int token) {
    auto it = m_screenPeers.find(quint32(id));
    if (it == m_screenPeers.end()) return;
    if (token != it->token) return;
    it->sink = nullptr;
    it->token = 0;
    delete it->dec;
    it->dec = nullptr;
    if (it->sws) { sws_freeContext(it->sws); it->sws = nullptr; }
    it->awaitKey = true;
    it->active = false;
}

int VideoEngine::attachScreenPreview(QVideoSink* sink) {
    m_scrPreview = sink;
    m_scrPreviewToken = ++m_attachSeq;
    return m_scrPreviewToken;
}

void VideoEngine::detachScreenPreview(int token) {
    if (token != m_scrPreviewToken) return;
    m_scrPreview = nullptr;
    m_scrPreviewToken = 0;
}

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
        p.holdQ.clear();               // придержанное относится к прошлой сессии
        if (p.active) {
            p.active = false;
            emit videoChanged(qint64(it.key()), false);
        }
    }
    for (auto it = m_screenPeers.begin(); it != m_screenPeers.end(); ++it) {
        Peer& p = it.value();
        delete p.dec;
        p.dec = nullptr;
        if (p.sws) { sws_freeContext(p.sws); p.sws = nullptr; }
        p.awaitKey = true;
        if (p.active) {
            p.active = false;
            emit screenVideoChanged(qint64(it.key()), false);
        }
    }
}

// Каждый join_ok — в т.ч. РЕКОННЕКТ: декодеры отстали от потока навсегда,
// а у анонимов ещё и id новые. Свежие keyframe попросят attach'и плиток.
// Пирам после нашего обрыва тоже нужен опорный кадр — шлём без просьбы.
void VideoEngine::onJoinOk() {
    resetPeers();
    m_keyNext = true;
    m_scrKeyNext = true;
}

void VideoEngine::onLeft() {
    resetPeers();
    m_live = false;                    // фаза осталась "live", но комнаты уже нет
    updateCapture();
    stopScreenCapture();
}

void VideoEngine::dropPeer(QHash<quint32, Peer>& peers, quint32 id, bool screen) {
    auto it = peers.find(id);
    if (it == peers.end()) return;
    if (it->active) {
        if (screen) emit screenVideoChanged(qint64(id), false);
        else        emit videoChanged(qint64(id), false);
    }
    delete it->dec;
    if (it->sws) sws_freeContext(it->sws);
    peers.erase(it);
}

void VideoEngine::onParticipantLeft(qint64 id) {
    dropPeer(m_peers, quint32(id), false);
    dropPeer(m_screenPeers, quint32(id), true);
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
    // Демонстрация на «Эко» идёт 5 к/с, но и там пауза в 5 с означает обрыв.
    for (auto it = m_screenPeers.begin(); it != m_screenPeers.end(); ++it) {
        Peer& p = it.value();
        if (!p.active || now - p.lastFrameAt <= 5000) continue;
        p.active = false;
        p.awaitKey = true;
        emit screenVideoChanged(qint64(it.key()), false);
    }
}

// ---------- приём ----------

void VideoEngine::requestKeyframe() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 wait = 1000 - (now - m_lastKeyReqAt);
    if (wait > 0) {
        // Не чаще раза в секунду (как веб) — но и НЕ терять просьбу: плитка,
        // родившаяся сразу после чужой просьбы, иначе ждала бы опорный кадр
        // по общей каденции, то есть до трёх секунд чёрного прямоугольника.
        if (!m_keyReqTimer->isActive()) m_keyReqTimer->start(int(wait));
        return;
    }
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
    if (f.type == Proto::VIDEO_JPEG || f.type == Proto::SCREEN_JPEG) {
        if (f.flags & Proto::FLAG_ENCRYPTED) return;   // legacy без WebCodecs
        const bool screen = (f.type == Proto::SCREEN_JPEG);
        paintJpeg((screen ? m_screenPeers : m_peers)[f.sender], f.sender, f.payload, screen);
        return;
    }
    if (f.type == Proto::VIDEO_CODED)
        routeCoded(m_peers, f.sender, f.flags, f.codec, f.ts, f.payload, false);
    else if (f.type == Proto::SCREEN_CODED)
        routeCoded(m_screenPeers, f.sender, f.flags, f.codec, f.ts, f.payload, true);
}

void VideoEngine::routeCoded(QHash<quint32, Peer>& peers, quint32 sender, quint8 flags,
                             quint8 codec, quint64 ts, const QByteArray& payload,
                             bool screen) {
    if (flags & Proto::FLAG_ENCRYPTED) return;     // E2E — M5
    if (codec != Proto::CODEC_H264 && codec != Proto::CODEC_VP8 &&
        codec != Proto::CODEC_VP9) return;         // незнакомое — молча мимо

    Peer& p = peers[sender];
    if (!p.sink) return;   // плитки (ещё) нет — не тратим CPU на декод в никуда

    // Декодер: создать под первый кадр; пересоздать, если отправитель сменил
    // кодек (правило §5.4 — у веба это смена браузера после реконнекта).
    if (!p.dec || p.dec->codec() != codec) {
        delete p.dec;
        p.dec = new VideoDecoder;
        if (!p.dec->open(codec)) { delete p.dec; p.dec = nullptr; return; }
        p.awaitKey = true;
    }

    // Дельта-кадры до опорного — мусор: молча дропаем и просим keyframe.
    const bool isKey = (flags & Proto::FLAG_KEYFRAME) != 0;
    if (p.awaitKey && !isKey) { requestKeyframe(); return; }
    p.awaitKey = false;

    const AVFrame* frame = p.dec->decode(payload, ts);

    // Декодер сломался (битый поток?) — правило §5.4: пересоздать и ждать
    // keyframe заново. Пересоздание случится само на следующем кадре.
    if (p.dec->failed()) {
        delete p.dec;
        p.dec = nullptr;
        p.awaitKey = true;
        requestKeyframe();
        return;
    }

    if (frame) deliver(p, sender, frame, qint64(ts), screen);
}

// AVFrame → QVideoFrame → плитка. sws_scale пишет прямо в плоскости
// QVideoFrame: для YUV420P это быстрое копирование с учётом stride, для
// экзотики (10-битный VP9 и т.п.) — честная конверсия. YUV→RGB на экране
// делает GPU при отрисовке — CPU в цвета не лезет.
void VideoEngine::deliver(Peer& p, quint32 sender, const AVFrame* f, qint64 tsMs,
                          bool screen) {
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

    // Синхронизация губ: звук доходит до ушей позже картинки (джиттер-буфер
    // плюс буфер звуковой карты), поэтому кадр, обогнавший свой звук,
    // придерживаем до его метки. Верхняя граница отсекает бессмыслицу
    // (сбитые часы, чужая шкала) — тогда рисуем сразу, как раньше.
    // Экран губами не шевелит: придерживать его под звук незачем, а на «Эко»
    // (5 к/с) это давало бы заметный лаг курсора. Рисуем сразу.
    const qint64 ph = (m_audio && !screen) ? m_audio->playheadMs(sender) : 0;
    const qint64 lead = ph ? tsMs - ph : 0;
    if (lead > 30 && lead < 1200) {
        p.holdQ.append({ vf, tsMs });
        if (p.holdQ.size() > 12) p.holdQ.removeFirst();   // очередь не копим
        if (!m_holdTimer->isActive()) m_holdTimer->start();
        return;
    }

    paint(p, sender, vf, screen);
}

void VideoEngine::paint(Peer& p, quint32 sender, const QVideoFrame& vf, bool screen) {
    if (!p.sink) return;
    p.sink->setVideoFrame(vf);

    p.lastFrameAt = QDateTime::currentMSecsSinceEpoch();
    if (!p.active) {
        p.active = true;
        if (screen) emit screenVideoChanged(qint64(sender), true);
        else        emit videoChanged(qint64(sender), true);  // «показывай видео»
    }
}

// Отдаём придержанные кадры, как только звук до них дотянулся.
void VideoEngine::drainHeld() {
    bool anyLeft = false;
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        Peer& p = it.value();
        while (!p.holdQ.isEmpty()) {
            const qint64 ph = m_audio ? m_audio->playheadMs(it.key()) : 0;
            // Звук пропал (мик выключили) — держать больше не за что.
            const qint64 lead = ph ? p.holdQ.first().ts - ph : 0;
            if (lead > 30) break;                  // ещё рано
            const Held h = p.holdQ.takeFirst();
            paint(p, it.key(), h.frame, false);   // придерживаем только камеру
        }
        if (!p.holdQ.isEmpty()) anyLeft = true;
    }
    if (!anyLeft) m_holdTimer->stop();
}

// Legacy-камера: целый JPEG в payload (≤480×360, ~10 к/с). Декодер не нужен —
// JPEG умеет Qt, а QVideoFrame с Qt 6.8 строится прямо из QImage.
void VideoEngine::paintJpeg(Peer& p, quint32 sender, const QByteArray& jpeg, bool screen) {
    if (!p.sink) return;
    const QImage img = QImage::fromData(jpeg, "JPEG");
    if (img.isNull()) return;

    p.sink->setVideoFrame(QVideoFrame(img));

    p.lastFrameAt = QDateTime::currentMSecsSinceEpoch();
    if (!p.active) {
        p.active = true;
        if (screen) emit screenVideoChanged(qint64(sender), true);
        else        emit videoChanged(qint64(sender), true);
    }
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
    if (!m_camera && !m_scrSession) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastForceAt < 500) return;
    m_lastForceAt = now;
    // Просьба одна на обе полосы: сервер рассылает KEYFRAME_REQ всем вещающим,
    // и новичку одинаково нужны и опорный кадр камеры, и опорный кадр экрана.
    m_keyNext = true;
    m_scrKeyNext = true;
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
    // Воркер ещё не отработал прошлый кадр: очередь копить нельзя — кадры в
    // ней стареют, а метка времени у них с момента съёмки, и получатель
    // увидит картинку позже звука. Лучше пропустить кадр, чем отстать.
    if (m_encInFlight > 0) return;
    m_lastEncodeAt = now;

    // Тяжёлый sws+encode — на кодирующем потоке. QVideoFrame неявно расшарен:
    // копия дешёвая, буфер общий; воркер маппит его read-only у себя.
    const bool forceKey = m_keyNext;
    m_keyNext = false;
    ++m_encInFlight;
    emit frameToEncode(frame, q.width, q.height, q.fps, q.bitrate, forceKey, now);
}

// ---------- отправка: демонстрация экрана (M7) ----------

// Единственный судья — сервер: слот демонстрации в комнате один, и снимать
// экран мы начинаем только увидев в подтверждении собственный id (§4.3).
void VideoEngine::onScreenSlotChanged() {
    const qint64 me = m_conf->myId();
    if (me != 0 && m_conf->screenId() == me) startScreenCapture();
    else                                     stopScreenCapture();
}

void VideoEngine::startScreenCapture() {
    if (m_scrSession) return;
    if (!m_sources || !m_sources->hasSelection()) {
        failScreen(QStringLiteral("Не выбран экран или окно для демонстрации."));
        return;
    }

    m_scrSession = new QMediaCaptureSession(this);
    m_scrSink = new QVideoSink(this);
    m_scrSession->setVideoSink(m_scrSink);
    connect(m_scrSink, &QVideoSink::videoFrameChanged, this, &VideoEngine::onScreenCapFrame);

    if (m_sources->isWindow()) {
        m_scrWindow = new QWindowCapture(this);
        m_scrWindow->setWindow(m_sources->selectedWindow());
        m_scrSession->setWindowCapture(m_scrWindow);
        // Окно закрыли посреди демонстрации — захват падает с ошибкой, и это
        // единственный сигнал о том, что показывать больше нечего.
        connect(m_scrWindow, &QWindowCapture::errorOccurred, this,
                [this](QWindowCapture::Error e, const QString& s) {
                    if (e != QWindowCapture::NoError)
                        failScreen(s.isEmpty()
                            ? QStringLiteral("Окно больше недоступно.") : s);
                });
        m_scrWindow->start();
    } else {
        QScreen* scr = m_sources->selectedScreen();
        if (!scr) {                    // монитор отключили между выбором и стартом
            stopScreenCapture();
            failScreen(QStringLiteral("Выбранный экран больше недоступен."));
            return;
        }
        // Курсор: захват монитора идёт через Desktop Duplication, а он отдаёт
        // кадр без курсора (у захвата ОКНА он есть — там другой механизм).
        // Значит для монитора рисуем сами; воркеру нужен физический
        // прямоугольник монитора, чтобы перевести координаты курсора в кадр.
        QRect physical = ScreenCursor::monitorRect(scr);
        if (physical.isEmpty())            // имя не совпало — считаем по Qt
            physical = QRect(scr->geometry().topLeft() * scr->devicePixelRatio(),
                             scr->geometry().size() * scr->devicePixelRatio());
        QMetaObject::invokeMethod(m_scrWorker, "setCursorSource",
                                  Qt::QueuedConnection, Q_ARG(QRect, physical));

        m_scrScreen = new QScreenCapture(this);
        m_scrScreen->setScreen(scr);
        m_scrSession->setScreenCapture(m_scrScreen);
        connect(m_scrScreen, &QScreenCapture::errorOccurred, this,
                [this](QScreenCapture::Error e, const QString& s) {
                    if (e != QScreenCapture::NoError)
                        failScreen(s.isEmpty()
                            ? QStringLiteral("Не удалось захватить экран.") : s);
                });
        m_scrScreen->start();
    }

    m_scrKeyNext = true;
    m_scrLastEncodeAt = 0;
}

void VideoEngine::stopScreenCapture() {
    if (m_scrScreen) { m_scrScreen->stop(); m_scrScreen->deleteLater(); m_scrScreen = nullptr; }
    if (m_scrWindow) { m_scrWindow->stop(); m_scrWindow->deleteLater(); m_scrWindow = nullptr; }
    if (m_scrSession) { m_scrSession->deleteLater(); m_scrSession = nullptr; }
    if (m_scrSink) { m_scrSink->deleteLater(); m_scrSink = nullptr; }
    if (m_scrWorker) {
        QMetaObject::invokeMethod(m_scrWorker, "reset", Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_scrWorker, "setCursorSource", Qt::QueuedConnection,
                                  Q_ARG(QRect, QRect()));   // курсор больше не наш
    }
    m_scrKeyNext = true;
    if (m_scrPreview) m_scrPreview->setVideoFrame(QVideoFrame());
    setScreenPreviewActive(false);
}

// Смена пресета качества на лету: перезапускаем, только если реально снимаем.
void VideoEngine::restartScreenCapture() {
    if (!m_scrSession) return;
    stopScreenCapture();
    onScreenSlotChanged();
}

void VideoEngine::failScreen(const QString& text) {
    stopScreenCapture();
    m_conf->setScreenShare(false);     // отпустить слот, иначе он повиснет за нами
    emit screenError(text);
}

void VideoEngine::setScreenPreviewActive(bool on) {
    if (m_screenPreviewActive == on) return;
    m_screenPreviewActive = on;
    emit screenPreviewActiveChanged();
}

void VideoEngine::onScreenCapFrame(const QVideoFrame& frame) {
    if (!frame.isValid()) return;

    // Своя сцена — сразу и без кодека (как превью камеры): видеть, что именно
    // ты показываешь, нужно немедленно.
    if (m_scrPreview) m_scrPreview->setVideoFrame(frame);
    setScreenPreviewActive(true);

    if (!m_live) return;
    const MediaSettings::CamPreset q = m_settings->screenPreset();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_scrLastEncodeAt < 1000 / q.fps) return;
    if (m_conf->bufferedBytes() > kMaxBufferedScreen) return;   // экран уступает дорогу
    if (m_scrInFlight > 0) return;                              // см. onCamFrame
    m_scrLastEncodeAt = now;

    const bool forceKey = m_scrKeyNext;
    m_scrKeyNext = false;
    ++m_scrInFlight;
    emit screenFrameToEncode(frame, q.width, q.height, q.fps, q.bitrate, forceKey, now);
}
