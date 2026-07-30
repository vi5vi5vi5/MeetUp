#include "VideoEngine.h"
#include "VideoSendWorker.h"
#include "VideoRecvWorker.h"
#include "ScreenCapturer.h"
#include "AudioEngine.h"
#include "MediaStats.h"
#include "../MediaSettings.h"
#include "../ScreenSources.h"
#include "../net/SignalingClient.h"
#include "../net/SignalingLink.h"
#include "../net/Protocol.h"
#include <QVideoSink>
#include <QVideoFrame>
#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QMediaCaptureSession>
#include <QThread>
#include <QDateTime>
#include <QTimer>
#include <QDebug>

// Затор в сокете, после которого кадры КАМЕРЫ пропускаются (§5.5).
static const qint64 kMaxBuffered = 1500000;

// Ступени качества демонстрации — множители пресетного битрейта.
//
// Ступени дискретные, а не плавная регулировка, и вот почему: сменить битрейт
// у открытого кодировщика нельзя, его надо переоткрыть, а переоткрытие стоит
// опорного кадра — то есть всплеска в том самом сокете, который мы пытаемся
// разгрузить. Плавный регулятор устраивал бы такой всплеск каждые несколько
// секунд. Пять ступеней с шагом около 30 % покрывают путь от 3.5 Мбит/с почти
// до 900 кбит/с и переключаются редко.
static const double kScreenRateLevels[] = { 1.0, 0.7, 0.5, 0.35, 0.25 };
static const int kScreenRateLevelCount =
    int(sizeof(kScreenRateLevels) / sizeof(kScreenRateLevels[0]));

VideoEngine::VideoEngine(SignalingClient* conf, MediaSettings* settings,
                         ScreenSources* sources, AudioEngine* audio,
                         MediaStats* stats, E2eCipher* cipher, QObject* parent)
    : QObject(parent), m_conf(conf), m_settings(settings), m_sources(sources),
      m_audio(audio), m_stats(stats)
{
    connect(conf, &SignalingClient::binaryFrame,       this, &VideoEngine::onBinaryFrame);
    connect(conf, &SignalingClient::joinOk,            this, &VideoEngine::onJoinOk);
    // Кадры видео идут из транспорта прямо на потоки декодирования, минуя нас:
    // тяжёлый декод не должен стоять в очереди GUI-потока. Обратно приезжает
    // готовый QVideoFrame — его рисует уже этот поток, иначе нельзя.
    m_recvThread = new QThread(this);
    m_recvThread->setObjectName("video-decode");
    m_scrRecvThread = new QThread(this);
    m_scrRecvThread->setObjectName("screen-decode");
    m_recv = new VideoRecvWorker(Proto::VIDEO_CODED, Proto::VIDEO_JPEG, cipher);
    m_scrRecv = new VideoRecvWorker(Proto::SCREEN_CODED, Proto::SCREEN_JPEG, cipher);
    m_recv->moveToThread(m_recvThread);
    m_scrRecv->moveToThread(m_scrRecvThread);
    connect(m_recvThread, &QThread::finished, m_recv, &QObject::deleteLater);
    connect(m_scrRecvThread, &QThread::finished, m_scrRecv, &QObject::deleteLater);

    SignalingLink* link = conf->mediaLink();
    connect(link, &SignalingLink::videoFrame, m_recv, &VideoRecvWorker::onFrame);
    connect(link, &SignalingLink::videoFrame, m_scrRecv, &VideoRecvWorker::onFrame);
    connect(m_recv, &VideoRecvWorker::frameReady, this,
            [this](quint32 sender, const QVideoFrame& vf, qint64 ts) {
                onDecoded(m_peers, sender, vf, ts, false);
            });
    connect(m_scrRecv, &VideoRecvWorker::frameReady, this,
            [this](quint32 sender, const QVideoFrame& vf, qint64 ts) {
                onDecoded(m_screenPeers, sender, vf, ts, true);
            });
    // Ограничитель частоты просьб об опорном кадре общий на обе полосы —
    // поэтому просят они через нас, а не сами.
    connect(m_recv, &VideoRecvWorker::keyframeNeeded, this, &VideoEngine::requestKeyframe);
    connect(m_scrRecv, &VideoRecvWorker::keyframeNeeded, this, &VideoEngine::requestKeyframe);
    connect(m_recv, &VideoRecvWorker::awaitKeyChanged, this,
            [this](quint32 sender, bool awaiting) { m_peers[sender].awaitKey = awaiting; });
    connect(m_scrRecv, &VideoRecvWorker::awaitKeyChanged, this,
            [this](quint32 sender, bool awaiting) { m_screenPeers[sender].awaitKey = awaiting; });
    // «Замок» участника один на все его полосы: сводим сюда голос и картинку.
    connect(m_recv, &VideoRecvWorker::peerLocked, this,
            [this](qint64 id, bool locked) { noteLocked(id, locked, false); });
    connect(m_scrRecv, &VideoRecvWorker::peerLocked, this,
            [this](qint64 id, bool locked) { noteLocked(id, locked, false); });
    if (audio)
        connect(audio, &AudioEngine::peerLocked, this,
                [this](qint64 id, bool locked) { noteLocked(id, locked, true); });
    m_recvThread->start();
    m_scrRecvThread->start();
    QMetaObject::invokeMethod(m_recv, &VideoRecvWorker::init, Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_scrRecv, &VideoRecvWorker::init, Qt::QueuedConnection);

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
    // Битрейт — не повод перезапускать захват: он не влияет ни на то, что мы
    // снимаем, ни на размер кадра. Достаточно уронить кодировщик, он откроется
    // на следующем кадре уже с новым потолком. Иначе смена настройки ждала бы
    // выдержку переоткрытия (4 с) и выглядела бы как «не сработало».
    connect(settings, &MediaSettings::screenBitrateChanged, this, [this] {
        resetScreenRate();             // новый потолок — снова с полного качества
        if (m_scrWorker)
            QMetaObject::invokeMethod(m_scrWorker, "reset", Qt::QueuedConnection);
    });
    // А вот курсор — просто флаг для воркера: перезапускать поток незачем.
    connect(settings, &MediaSettings::screenCursorChanged,
            this, &VideoEngine::applyCursorSetting);
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
    m_worker = new VideoSendWorker(Proto::VIDEO_CODED, cipher);   // без parent: свой поток
    m_scrWorker = new VideoSendWorker(Proto::SCREEN_CODED, cipher);
    m_worker->moveToThread(m_encThread);
    m_scrWorker->moveToThread(m_scrThread);
    connect(m_encThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_scrThread, &QThread::finished, m_scrWorker, &QObject::deleteLater);
    connect(this, &VideoEngine::frameToEncode, m_worker, &VideoSendWorker::encode);
    connect(this, &VideoEngine::screenFrameToEncode, m_scrWorker, &VideoSendWorker::encode);
    // Готовый пакет уходит с потока кодирования ПРЯМО на поток сокета. Раньше
    // он делал крюк через GUI-поток (packetReady -> лямбда -> invokeMethod на
    // link), и каждый кадр ждал, пока интерфейс освободится, — а тот стоит на
    // синхронизации с отрисовкой. В медиапути GUI-потоку делать нечего.
    connect(m_worker, &VideoSendWorker::packetReady, link, &SignalingLink::sendBinary);
    connect(m_scrWorker, &VideoSendWorker::packetReady, link, &SignalingLink::sendBinary);
    // …а числа для «Диагностики» — сюда: MediaStats живёт на GUI-потоке.
    connect(m_worker, &VideoSendWorker::packetSent, this,
            [this](int bytes) { m_stats->noteTxVideo(false, bytes); });
    connect(m_scrWorker, &VideoSendWorker::packetSent, this,
            [this](int bytes) { m_stats->noteTxVideo(true, bytes); });
    // Кодировщик открылся: кодек и размер кадра — в «Диагностику». Спрашивать
    // об этом настройки нельзя: там «Авто», а кадр может быть меньше пресета,
    // если камера столько не даёт.
    connect(m_worker, &VideoSendWorker::encoderOpened, this,
            [this](int c, int w, int h, bool hw) { noteEncoderOpened(false, c, w, h, hw); });
    connect(m_scrWorker, &VideoSendWorker::encoderOpened, this,
            [this](int c, int w, int h, bool hw) { noteEncoderOpened(true, c, w, h, hw); });
    // Занятость воркеров движок спрашивает напрямую (VideoSendWorker::busy) —
    // сигнала об освобождении больше нет, см. комментарий там.
    // Выбранного кодека в сборке не оказалось — сказать человеку, а не менять
    // молча. Полосу называем: у камеры и экрана настройки разные.
    connect(m_worker, &VideoSendWorker::codecFallback, this,
            [this](int req, int act) { noteCodecFallback(false, req, act); });
    connect(m_scrWorker, &VideoSendWorker::codecFallback, this,
            [this](int req, int act) { noteCodecFallback(true, req, act); });
    m_encThread->start();
    m_scrThread->start();

    // Захват экрана создаём НА потоке экрана и уже после его запуска: WGC хочет
    // апартамент COM = MTA (QThread его и даёт, GUI-поток Qt — нет), а сторож
    // свёрнутого окна внутри требует работающего цикла событий.
    QMetaObject::invokeMethod(m_scrWorker, [this] {
        m_scrCapture = new ScreenCapturer;
    }, Qt::BlockingQueuedConnection);
    // Кадр приезжает с потока пула WGC — сюда queued, потому что дальше идут
    // превью (только GUI-поток) и пейсер.
    connect(m_scrCapture, &ScreenCapturer::frameReady,
            this, &VideoEngine::onScreenCapFrame, Qt::QueuedConnection);
    connect(m_scrCapture, &ScreenCapturer::failed, this,
            [this](const QString& text) { failScreen(text); }, Qt::QueuedConnection);
    connect(m_scrCapture, &ScreenCapturer::suspendedChanged,
            this, &VideoEngine::onScreenSuspended, Qt::QueuedConnection);

    // Досылка последнего кадра, когда экран неподвижен (см. onScreenRepeat).
    m_scrRepeatTimer = new QTimer(this);
    connect(m_scrRepeatTimer, &QTimer::timeout, this, &VideoEngine::onScreenRepeat);

    // Выбор кодека: воркеру он нужен до первого кадра, дальше — по изменению.
    connect(settings, &MediaSettings::camCodecChanged, this, &VideoEngine::applyCodecPrefs);
    connect(settings, &MediaSettings::screenCodecChanged, this, &VideoEngine::applyCodecPrefs);
    applyCodecPrefs();

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
    // Захват создавали на потоке экрана — там же и разрушаем, до его остановки:
    // внутри объекты WinRT и таймер, привязанные именно к тому потоку.
    if (m_scrCapture) {
        QMetaObject::invokeMethod(m_scrCapture, [this] {
            delete m_scrCapture;
            m_scrCapture = nullptr;
        }, Qt::BlockingQueuedConnection);
    }
    if (m_encThread) {                         // остановить потоки кодирования
        m_encThread->quit();
        m_encThread->wait();                   // finished -> deleteLater воркера
    }
    if (m_scrThread) {
        m_scrThread->quit();
        m_scrThread->wait();
    }
    // Декодеры живут на своих потоках — там же и разрушаются.
    QMetaObject::invokeMethod(m_recv, &VideoRecvWorker::shutdown,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_scrRecv, &VideoRecvWorker::shutdown,
                              Qt::BlockingQueuedConnection);
    m_recvThread->quit();
    m_recvThread->wait();
    m_scrRecvThread->quit();
    m_scrRecvThread->wait();
    m_peers.clear();
    m_screenPeers.clear();
}

// ---------- связь с плитками ----------

// Плитка родилась. Если декодер жив и поток за время пересадки не рвался —
// просто переставляем приёмник: ни чёрного кадра, ни лишнего KEYFRAME_REQ.
// Состояние «картинка идёт» новая плитка спрашивает сама (isLive), потому что
// videoChanged сообщает только переход.
int VideoEngine::attach(qint64 id, QVideoSink* sink) {
    Peer& p = m_peers[quint32(id)];    // operator[] создаст пустого
    p.sink = sink;
    p.token = ++m_attachSeq;
    // Поток отправителя прерван — не оставляем на плитке застывший кадр.
    // Опорный кадр попросит воркер: только он знает, годен ли ещё декодер.
    if (p.awaitKey) p.active = false;
    tellWanted(false, quint32(id), true);
    return p.token;
}

// Кому сейчас нужны кадры. Воркер по этому же вызову решает, просить ли
// опорный кадр: у него состояние декодера, у нас — только плитки.
void VideoEngine::tellWanted(bool screen, quint32 sender, bool wanted) {
    VideoRecvWorker* w = screen ? m_scrRecv : m_recv;
    QMetaObject::invokeMethod(w, [w, sender, wanted] { w->setWanted(sender, wanted); },
                              Qt::QueuedConnection);
}

// Декодер НЕ трогаем: плитку могло пересоздать на ровном месте (любое
// изменение списка участников в QML пересобирает делегаты), и выбрасывать
// состояние потока ради этого — значит гарантированно моргнуть картинкой.
// Кадры без плитки дропает воркер, он же поднимет awaitKey и добьёт декодер
// осиротевшего пира, если плитка так и не вернётся.
void VideoEngine::detach(qint64 id, int token) {
    auto it = m_peers.find(quint32(id));
    if (it == m_peers.end()) return;
    if (token != it->token) return;    // привязку уже перехватила новая плитка
    it->sink = nullptr;
    it->token = 0;
    it->holdQ.clear();                 // рисовать больше некуда
    tellWanted(false, quint32(id), false);
}

bool VideoEngine::isLive(qint64 id) const {
    const auto it = m_peers.constFind(quint32(id));
    return it != m_peers.constEnd() && it->active;
}

bool VideoEngine::isScreenLive(qint64 id) const {
    const auto it = m_screenPeers.constFind(quint32(id));
    return it != m_screenPeers.constEnd() && it->active;
}

bool VideoEngine::isLocked(qint64 id) const {
    return m_lockedAudio.contains(id) || m_lockedVideo.contains(id);
}

// Заперт участник или нет — вопрос про человека, а не про полосу: сигнал
// наружу уходит, только когда меняется общий ответ.
void VideoEngine::noteLocked(qint64 id, bool locked, bool fromAudio) {
    QSet<qint64>& set = fromAudio ? m_lockedAudio : m_lockedVideo;
    const bool was = isLocked(id);
    if (locked) set.insert(id);
    else        set.remove(id);
    const bool now = isLocked(id);
    if (was != now) emit lockedChanged(id, now);
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

// Предпросмотр в настройках. Сам факт привязки — и есть просьба «снимай»:
// отдельного выключателя нет намеренно, иначе QML мог бы забыть его погасить
// и оставить камеру включённой (с горящим светодиодом) после закрытия окна.
int VideoEngine::attachPreviewExtra(QVideoSink* sink) {
    m_previewExtra = sink;
    m_previewExtraToken = ++m_attachSeq;
    updateCapture();
    return m_previewExtraToken;
}

void VideoEngine::detachPreviewExtra(int token) {
    if (token != m_previewExtraToken) return;
    m_previewExtra.clear();
    m_previewExtraToken = 0;
    updateCapture();               // камера гаснет, если тумблер выключен
}

// Сцена демонстрации — та же механика, только полоса другая.
int VideoEngine::attachScreen(qint64 id, QVideoSink* sink) {
    Peer& p = m_screenPeers[quint32(id)];
    p.sink = sink;
    p.token = ++m_attachSeq;
    if (p.awaitKey) p.active = false;  // см. attach()
    tellWanted(true, quint32(id), true);
    return p.token;
}

void VideoEngine::detachScreen(qint64 id, int token) {
    auto it = m_screenPeers.find(quint32(id));
    if (it == m_screenPeers.end()) return;
    if (token != it->token) return;
    it->sink = nullptr;
    it->token = 0;
    tellWanted(true, quint32(id), false);
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
    // Декодеры сбрасывают воркеры у себя; привязки плиток при этом целы, и
    // сразу после сброса они попросят опорный кадр заново.
    QMetaObject::invokeMethod(m_recv, &VideoRecvWorker::reset, Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_scrRecv, &VideoRecvWorker::reset, Qt::QueuedConnection);
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        Peer& p = it.value();
        p.awaitKey = true;
        p.holdQ.clear();               // придержанное относится к прошлой сессии
        if (p.active) {
            p.active = false;
            emit videoChanged(qint64(it.key()), false);
        }
        if (p.sink) tellWanted(false, it.key(), true);   // сброс стёр и «нужен»
    }
    for (auto it = m_screenPeers.begin(); it != m_screenPeers.end(); ++it) {
        Peer& p = it.value();
        p.awaitKey = true;
        if (p.active) {
            p.active = false;
            emit screenVideoChanged(qint64(it.key()), false);
        }
        if (p.sink) tellWanted(true, it.key(), true);
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
    peers.erase(it);
    VideoRecvWorker* w = screen ? m_scrRecv : m_recv;
    QMetaObject::invokeMethod(w, [w, id] { w->dropPeer(id); }, Qt::QueuedConnection);
}

void VideoEngine::onParticipantLeft(qint64 id) {
    dropPeer(m_peers, quint32(id), false);
    dropPeer(m_screenPeers, quint32(id), true);
}

// Сторож: вещатель умер молча (вкладку убили, ноутбук уснул) — через 5 с
// прячем замороженный кадр, плитка возвращается к аватару (правило §5.4).
// Осиротевшие декодеры прибирает у себя воркер (VideoRecvWorker::sweep) —
// здесь остаётся только видимая часть: убрать с плитки застывший кадр.
void VideoEngine::sweepStale() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        Peer& p = it.value();
        if (!p.active || now - p.lastFrameAt <= 5000) continue;
        p.active = false;
        emit videoChanged(qint64(it.key()), false);
    }
    // Демонстрация на «Эко» идёт 5 к/с, но и там пауза в 5 с означает обрыв.
    for (auto it = m_screenPeers.begin(); it != m_screenPeers.end(); ++it) {
        Peer& p = it.value();
        if (!p.active || now - p.lastFrameAt <= 5000) continue;
        p.active = false;
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

// Из транспорта сюда доезжает только служебное: кадры видео разбирают воркеры
// на своих потоках, звук — аудиопоток.
void VideoEngine::onBinaryFrame(const QByteArray& d) {
    Proto::FrameV2 f;
    if (!Proto::unpack(d, f)) return;
    if (f.type == Proto::KEYFRAME_REQ) forceKeyframe();   // просят опорный кадр
}

// Готовый кадр от воркера. Здесь он попадает на GUI-поток впервые — и только
// потому, что QVideoSink плитки живёт именно тут.
void VideoEngine::onDecoded(QHash<quint32, Peer>& peers, quint32 sender,
                            const QVideoFrame& vf, qint64 tsMs, bool screen) {
    Peer& p = peers[sender];
    if (!p.sink) return;               // плитка исчезла, пока кадр ехал

    // Синхронизация губ: звук доходит до ушей позже картинки (джиттер-буфер
    // плюс буфер звуковой карты), поэтому кадр, обогнавший свой звук,
    // придерживаем до его метки. Верхняя граница отсекает бессмыслицу
    // (сбитые часы, чужая шкала) — тогда рисуем сразу, как раньше.
    // Экран губами не шевелит: придерживать его под звук незачем, а на «Эко»
    // (5 к/с) это давало бы заметный лаг курсора. Рисуем сразу.
    const qint64 ph = (m_audio && !screen) ? m_audio->playheadMs(sender) : 0;
    const qint64 lead = ph ? tsMs - ph : 0;
    if (lead > 30 && lead < 1200) {
        m_stats->noteSyncHold(lead);      // на столько картинка ждёт свой звук
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
    m_stats->noteRxFrame(sender);

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

// ---------- отправка ----------

void VideoEngine::onPhase() {
    m_live = (m_conf->phase() == "live");
    updateCapture();
}

void VideoEngine::onLocalState(bool /*mic*/, bool cam) {
    m_camOn = cam;
    updateCapture();
}

// Захват идёт <=> мы в эфире И камера включена — ИЛИ открыт предпросмотр в
// настройках. Все дороги ведут сюда. Разница между этими двумя случаями одна,
// но важная: предпросмотр никуда не отправляет (см. onCamFrame).
void VideoEngine::updateCapture() {
    const bool want = (m_live && m_camOn) || m_previewExtra != nullptr;
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
    m_nextDueMs = 0;               // первый кадр нового захвата — сразу
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
    if (m_previewExtra) m_previewExtra->setVideoFrame(QVideoFrame());
    setPreviewActive(false);
    m_stats->noteTxOff(false);
}

void VideoEngine::setPreviewActive(bool on) {
    if (m_previewActive == on) return;
    m_previewActive = on;
    emit previewActiveChanged();
}

// Просьба прислать опорный кадр (KEYFRAME_REQ, новичок, реконнект).
// Rate-limit 500 мс — как у веба: спам запросов не роняет битрейт.
void VideoEngine::forceKeyframe() {
    if (!m_camera && !m_scrCapturing) return;
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
    if (m_previewExtra) m_previewExtra->setVideoFrame(frame);
    setPreviewActive(true);

    // Тумблер камеры выключен, а захват идёт — значит открыт предпросмотр в
    // настройках. Кадр остаётся на этом компьютере: ни кодирования, ни сети.
    if (!m_live || !m_camOn) return;
    const MediaSettings::CamPreset q = m_settings->camPreset();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 period = qMax<qint64>(1, 1000 / q.fps);
    if (now < m_nextDueMs) return;                           // ещё рано
    // Дальше кадр обязан был уйти — всё, что его останавливает, идёт в счёт
    // пропусков: именно из них складывается «дёргается картинка».
    m_stats->noteTxAttempt(false);
    if (m_conf->bufferedBytes() > kMaxBuffered) {            // затор — кадр в мусор (§5.5)
        m_stats->noteTxDrop(false);
        return;
    }
    // Воркер ещё не отработал прошлый кадр: очередь копить нельзя — кадры в
    // ней стареют, а метка времени у них с момента съёмки, и получатель
    // увидит картинку позже звука. Лучше пропустить кадр, чем отстать.
    if (m_worker->busy()) { m_stats->noteTxDrop(false); return; }
    // Сдвигаем срок ровно на период — так частота держится ровной. Но если
    // источник молчал дольше периода, догонять пропущенное нельзя: иначе после
    // паузы разом проскочит пачка кадров.
    m_nextDueMs = (m_nextDueMs < now - period) ? now + period : m_nextDueMs + period;

    // Тяжёлый sws+encode — на кодирующем потоке. QVideoFrame неявно расшарен:
    // копия дешёвая, буфер общий; воркер маппит его read-only у себя.
    const bool forceKey = m_keyNext;
    m_keyNext = false;
    m_worker->noteQueued();
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
    if (m_scrCapturing) return;
    if (!m_sources || !m_sources->hasSelection()) {
        failScreen(QStringLiteral("Не выбран экран или окно для демонстрации."));
        return;
    }

    // Захват идёт НЕ через Qt, а своим ScreenCapturer поверх
    // Windows.Graphics.Capture: у QScreenCapture частота была 27 к/с на
    // мониторе 60 Гц без всякой возможности на это повлиять, а отдельное окно
    // он снимал другим механизмом со своими особенностями. Запасного пути на
    // Qt сознательно НЕТ: целевая система — Windows 11, а старые системы
    // обслуживает веб-клиент.
    if (!ScreenCapturer::isSupported()) {
        failScreen(QStringLiteral(
            "Захват экрана недоступен в этой версии Windows. "
            "Подключитесь к встрече через браузер."));
        return;
    }

    void* handle = m_sources->isWindow() ? m_sources->selectedWindowHandle()
                                         : m_sources->selectedMonitorHandle();
    if (!handle) {
        failScreen(m_sources->isWindow()
                       ? QStringLiteral("Выбранное окно больше недоступно.")
                       : QStringLiteral("Выбранный экран больше недоступен."));
        return;
    }

    const bool wantWindow = m_sources->isWindow();
    const bool cursor = m_settings->screenCursor();
    // Уменьшение и перевод в NV12 отдаём видеокарте: она сделает это до того,
    // как кадр попадёт в обычную память, и вместо 33 МБ на 4К обратно поедет 3.
    // Воркеру достанется кадр уже нужного размера, и его sws_scale выродится в
    // копию. Рамка — та же, что у пресета; «Источник» даёт рамку заведомо
    // больше любого экрана, то есть уменьшать нечего.
    const MediaSettings::CamPreset preset = m_settings->screenPreset();
    const QSize box(preset.width, preset.height);
    bool started = false;
    // Захватчик живёт на потоке кодирования: WGC требует апартамента COM = MTA,
    // а GUI-поток Qt — STA. Заодно там уже есть цикл событий для его сторожа.
    QMetaObject::invokeMethod(m_scrCapture, [this, handle, wantWindow, cursor, box, &started] {
        m_scrCapture->setTargetBox(box);
        started = wantWindow ? m_scrCapture->startWindow(handle, cursor)
                             : m_scrCapture->startMonitor(handle, cursor);
    }, Qt::BlockingQueuedConnection);
    if (!started) {                    // подробность уже уехала сигналом failed
        stopScreenCapture();
        return;
    }

    m_scrCapturing = true;
    m_scrKeyNext = true;
    m_scrNextDueMs = 0;
    m_scrPreviewDueMs = 0;             // первый кадр — сразу на сцену
    m_scrSuspended = false;
    resetScreenRate();                 // новая демонстрация начинается с полного качества
    // Повтор последнего кадра: WGC присылает кадр, только когда композитор
    // что-то перерисовал, и на неподвижном экране поток бы просто прерывался.
    // Нам нужен ровный — см. onScreenRepeat.
    m_scrRepeatTimer->start(int(qMax<qint64>(1, 1000 / m_settings->screenPreset().fps)));
}

void VideoEngine::stopScreenCapture() {
    m_scrCapturing = false;
    m_scrRepeatTimer->stop();
    if (m_scrCapture)
        QMetaObject::invokeMethod(m_scrCapture, [this] { m_scrCapture->stop(); },
                                  Qt::BlockingQueuedConnection);
    m_scrLastFrame = QVideoFrame();
    m_scrSuspended = false;
    if (m_scrWorker)
        QMetaObject::invokeMethod(m_scrWorker, "reset", Qt::QueuedConnection);
    m_scrKeyNext = true;
    if (m_scrPreview) m_scrPreview->setVideoFrame(QVideoFrame());
    setScreenPreviewActive(false);
    m_stats->noteTxOff(true);
}

// Смена пресета качества на лету: перезапускаем, только если реально снимаем.
void VideoEngine::restartScreenCapture() {
    if (!m_scrCapturing) return;
    stopScreenCapture();
    onScreenSlotChanged();
}

void VideoEngine::failScreen(const QString& text) {
    stopScreenCapture();
    m_conf->setScreenShare(false);     // отпустить слот, иначе он повиснет за нами
    emit screenError(text);
}

// ---------- выбор кодека ----------

// Идентификатор из настроек -> байт кодека протокола. Незнакомое и "auto" -> 0
// (порядок по умолчанию: H.264, при неудаче VP8).
static quint8 protoOfCodec(const QString& id) {
    if (id == "h264") return Proto::CODEC_H264;
    if (id == "vp8")  return Proto::CODEC_VP8;
    if (id == "vp9")  return Proto::CODEC_VP9;
    // "hevc" и "av1" сюда пока не попадают: в настройках демонстрации они
    // приглушены, и выбрать их нельзя. Своих байтов в протоколе у них тоже
    // ещё нет — появятся вместе с обратной связью «не понимаю такой кодек»,
    // без которой выпускать их нельзя (см. SettingsShare.qml).
    return 0;
}

static QString codecName(int proto) {
    switch (proto) {
    case Proto::CODEC_H264: return QStringLiteral("H.264");
    case Proto::CODEC_VP8:  return QStringLiteral("VP8");
    case Proto::CODEC_VP9:  return QStringLiteral("VP9");
    default:                return QStringLiteral("другой кодек");
    }
}

// Кодек живёт в воркере (энкодер держит состояние потока кадров), поэтому
// настройка едет туда queued-вызовом. Смена кодека переоткрывает энкодер, а
// приёмники пересоздают декодеры сами — они сверяют кодек каждого кадра.
void VideoEngine::applyCodecPrefs() {
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setCodecPreference", Qt::QueuedConnection,
                                  Q_ARG(int, protoOfCodec(m_settings->camCodec())));
    if (m_scrWorker)
        QMetaObject::invokeMethod(m_scrWorker, "setCodecPreference", Qt::QueuedConnection,
                                  Q_ARG(int, protoOfCodec(m_settings->screenCodec())));
}

void VideoEngine::noteEncoderOpened(bool screen, int codec, int width, int height,
                                    bool hardware) {
    // Пометка идёт только у аппаратного: «H.264» без уточнения и так означает
    // процессор, а строка в «Диагностике» и без того длинная.
    const QString name = hardware
        ? QStringLiteral("%1 (аппаратный)").arg(codecName(codec))
        : codecName(codec);
    m_stats->noteEncoder(screen, name, width, height);
}

void VideoEngine::noteCodecFallback(bool screen, int requested, int actual) {
    emit codecNotice(QStringLiteral("%1: %2 недоступен, идёт %3")
                         .arg(screen ? QStringLiteral("Демонстрация")
                                     : QStringLiteral("Камера"),
                              codecName(requested), codecName(actual)));
}

// Курсор дорисовывается только при показе МОНИТОРА (у захвата окна он свой) и
// только если пользователь этого хочет. Пустой прямоугольник = не рисовать,
// поэтому переключение на лету не требует перезапуска захвата.
void VideoEngine::applyCursorSetting() {
    if (!m_scrCapture) return;
    const bool on = m_settings->screenCursor();
    // Курсор рисует система, а не мы: раньше здесь передавался физический
    // прямоугольник монитора, по которому воркер сам накладывал стрелку —
    // со своим масштабированием и своими ошибками в нём.
    QMetaObject::invokeMethod(m_scrCapture, [this, on] { m_scrCapture->setCursorEnabled(on); },
                              Qt::QueuedConnection);
}

// Окно свернули: кадров не будет, пока его не развернут. Отпускать слот
// демонстрации при этом неправильно — человек свернул окно на секунду, — но и
// молчать нельзя: у зрителей сцена замрёт на последнем кадре без объяснений.
// Поэтому говорим им текстом и перестаём слать повторы (слать один и тот же
// кадр в никуда незачем), а как развернут — поток пойдёт сам.
void VideoEngine::onScreenSuspended(bool suspended) {
    if (m_scrSuspended == suspended) return;
    m_scrSuspended = suspended;
    if (suspended)
        emit screenError(QStringLiteral(
            "Окно свёрнуто — демонстрация приостановлена. Разверните окно."));
}

// Повтор последнего кадра. WGC присылает кадр только на изменение картинки:
// на неподвижном экране поток бы прерывался совсем, а это ровно то, из-за чего
// у аппаратного кодировщика застревают внутри последние два кадра, и зритель
// продолжает видеть картинку до остановки. Поэтому досылаем последний кадр по
// расписанию пейсера — он же и решит, не рано ли.
void VideoEngine::onScreenRepeat() {
    if (!m_scrCapturing || m_scrSuspended || !m_scrLastFrame.isValid()) return;
    sendScreenFrame(m_scrLastFrame);
}

void VideoEngine::resetScreenRate() {
    m_scrRateLevel = 0;
    m_scrBusySamples = 0;
    m_scrCleanSamples = 0;
    m_scrRateSampleMs = 0;
}

// Что делать, когда канал не вмещает то, что мы производим.
//
// Раньше ответ был один: выбросить кадр, как только в сокете накопилось больше
// 350 000 байт. Это плохо с двух сторон. Порог фиксированный: при 3.5 Мбит/с
// один опорный кадр 1080p (до 250 КБ) пробивал его сам, после чего выбрасывалось
// ВСЁ, пока сокет не разгрузится, — отсюда замирания на полсекунды. А реакция
// одна и та же независимо от причины: узкий канал лечился не снижением
// качества, а выбрасыванием кадров, то есть рывками вместо мягкой картинки.
//
// Теперь качество снижается ступенями, а кадры роняются только если очередь
// всё равно не рассасывается. Порог считается из ТЕКУЩЕГО битрейта — полсекунды
// того, что мы сами производим, — поэтому после снижения качества он опускается
// вместе с потоком, а не остаётся прежним.
int VideoEngine::screenBitrate(int presetBitrate, qint64 nowMs, qint64* queueBudget) {
    const int target = int(presetBitrate * kScreenRateLevels[m_scrRateLevel]);
    // Бюджет очереди: полсекунды текущего потока, но не меньше 128 КБ — иначе
    // на низких ступенях в него не влезал бы даже один опорный кадр.
    const qint64 budget = qBound(qint64(128000), qint64(target) / 8 / 2, qint64(1500000));
    if (queueBudget) *queueBudget = budget;

    // Пробы раз в 500 мс: ступень должна двигаться по устойчивой картине, а не
    // по одному всплеску (всплеск как раз и бывает от опорного кадра).
    if (nowMs - m_scrRateSampleMs < 500) return target;
    m_scrRateSampleMs = nowMs;

    const qint64 queued = m_conf->bufferedBytes();
    if (queued > budget)            { ++m_scrBusySamples;  m_scrCleanSamples = 0; }
    else if (queued < budget / 4)   { ++m_scrCleanSamples; m_scrBusySamples = 0; }

    // Несимметрично намеренно: вниз — после 1.5 с затора, вверх — только после
    // 10 с чистой очереди. Ронять качество надо быстро, поднимать осторожно,
    // иначе на канале, который еле держит текущую ступень, получаются качели:
    // поднялись, захлебнулись, упали, поднялись.
    if (m_scrBusySamples >= 3 && m_scrRateLevel < kScreenRateLevelCount - 1) {
        ++m_scrRateLevel;
        m_scrBusySamples = 0;
        qInfo() << "VideoEngine: демонстрация -> ступень" << m_scrRateLevel
                << "битрейт" << int(presetBitrate * kScreenRateLevels[m_scrRateLevel]);
    } else if (m_scrCleanSamples >= 20 && m_scrRateLevel > 0) {
        --m_scrRateLevel;
        m_scrCleanSamples = 0;
        qInfo() << "VideoEngine: демонстрация -> ступень" << m_scrRateLevel
                << "битрейт" << int(presetBitrate * kScreenRateLevels[m_scrRateLevel]);
    }
    return target;
}

void VideoEngine::setScreenPreviewActive(bool on) {
    if (m_screenPreviewActive == on) return;
    m_screenPreviewActive = on;
    emit screenPreviewActiveChanged();
}

// Свежий кадр от захвата. Приезжает queued с потока пула WGC: рисовать превью и
// трогать пейсер можно только здесь, на GUI-потоке.
void VideoEngine::onScreenCapFrame(const QVideoFrame& frame) {
    if (!frame.isValid()) return;

    // Запоминаем для повтора: на неподвижном экране новых кадров не будет,
    // а поток в сеть должен идти ровно (см. onScreenRepeat).
    m_scrLastFrame = frame;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 period = qMax<qint64>(1, 1000 / m_settings->screenPreset().fps);

    // Своя сцена — сразу и без кодека (как превью камеры): видеть, что именно
    // ты показываешь, нужно немедленно. Но НЕ на каждый кадр захвата: кадр
    // монитора 4К — это ~33 МБ загрузки текстуры, и шестьдесят таких в секунду
    // занимают render-поток целиком, а вместе с ним и GUI-поток, который на
    // него синхронизируется. Быстрее шестнадцатой доли секунды глаз всё равно
    // не просит, а чаще, чем уходит в эфир, показывать попросту незачем.
    const qint64 previewPeriod = qMax<qint64>(66, period);
    if (now >= m_scrPreviewDueMs) {
        m_scrPreviewDueMs = now + previewPeriod;
        if (m_scrPreview) m_scrPreview->setVideoFrame(frame);
    }
    setScreenPreviewActive(true);

    sendScreenFrame(frame);
}

// Один путь для свежего кадра и для повтора: пейсер, регулятор качества,
// отбраковка при заторе и передача воркеру. Повтор отличается только тем, что
// содержимое то же самое — и кодировщик превратит его в считанные байты.
void VideoEngine::sendScreenFrame(const QVideoFrame& frame) {
    if (!frame.isValid()) return;

    const MediaSettings::CamPreset q = m_settings->screenPreset();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 period = qMax<qint64>(1, 1000 / q.fps);

    if (!m_live) return;
    if (now < m_scrNextDueMs) return;                           // ещё рано
    m_stats->noteTxAttempt(true);

    // Ступень качества и бюджет очереди считаются вместе — см. screenBitrate.
    qint64 budget = 0;
    const int bitrate = screenBitrate(q.bitrate, now, &budget);

    // Очередь не рассасывается даже после снижения качества — только теперь
    // роняем кадр. Экран уступает дорогу голосу и камере первым: лицо к
    // пропаже кадра чувствительнее, чем слайд, а кадры экрана самые тяжёлые.
    if (m_conf->bufferedBytes() > budget) {
        m_stats->noteTxDrop(true);
        return;
    }
    if (m_scrWorker->busy()) { m_stats->noteTxDrop(true); return; }   // см. onCamFrame
    m_scrNextDueMs = (m_scrNextDueMs < now - period) ? now + period
                                                    : m_scrNextDueMs + period;

    const bool forceKey = m_scrKeyNext;
    m_scrKeyNext = false;
    m_scrWorker->noteQueued();
    emit screenFrameToEncode(frame, q.width, q.height, q.fps, bitrate, forceKey, now);
}
