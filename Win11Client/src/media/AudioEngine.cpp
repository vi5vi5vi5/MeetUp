#include "AudioEngine.h"
#include "AudioWorker.h"
#include "MediaStats.h"
#include "../MediaSettings.h"
#include "../net/SignalingClient.h"
#include "../net/SignalingLink.h"
#include <QThread>

AudioEngine::AudioEngine(SignalingClient* conf, MediaSettings* settings,
                         MediaStats* stats, E2eCipher* cipher, QObject* parent)
    : QObject(parent), m_conf(conf), m_settings(settings), m_stats(stats)
{
    // ---- поток звука ----
    m_thread = new QThread(this);
    m_thread->setObjectName("audio");
    m_worker = new AudioWorker(cipher);      // без parent: переезжает на свой поток
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    // Кадры звука идут из транспорта прямо сюда, минуя GUI-поток, и уходят той
    // же дорогой обратно. Это и есть весь смысл затеи: пока пользователь тянет
    // рамку окна, разговор не должен ждать интерфейс.
    SignalingLink* link = conf->mediaLink();
    connect(link, &SignalingLink::audioFrame, m_worker, &AudioWorker::onFrame);
    connect(m_worker, &AudioWorker::packetReady, link, &SignalingLink::sendBinary);

    // Обратно на GUI — только то, что видит человек: индикаторы и числа.
    connect(m_worker, &AudioWorker::txAudio, this, [this](bool screen, int bytes) {
        m_stats->noteTxAudio(screen, bytes);
        });
    connect(m_worker, &AudioWorker::micLevel, this, [this](qreal level) {
        m_settings->reportMicLevel(level);
        });
    connect(m_worker, &AudioWorker::selfSpeaking, this, [this] {
        m_conf->markSelfSpeaking();
        });
    connect(m_worker, &AudioWorker::speaking, this, [this](qint64 id) {
        m_conf->markSpeaking(id);
        });
    connect(m_worker, &AudioWorker::screenLive, this, &AudioEngine::setScreenLive);
    connect(m_worker, &AudioWorker::peerLocked, this, &AudioEngine::peerLocked);
    connect(m_worker, &AudioWorker::screenFailed, this, [this](const QString& t) {
        m_settings->setScreenAudio(false);   // не оставляем тумблер обманкой
        emit screenAudioError(t);
        });

    m_thread->start();
    QMetaObject::invokeMethod(m_worker, &AudioWorker::init, Qt::QueuedConnection);

    // ---- состояние комнаты ----
    connect(conf, &SignalingClient::phaseChanged, this, &AudioEngine::onPhase);
    connect(conf, &SignalingClient::localStateChanged, this, &AudioEngine::onLocalState);
    connect(conf, &SignalingClient::left, this, &AudioEngine::onLeft);
    connect(conf, &SignalingClient::joinOk, this, [this] {
        QMetaObject::invokeMethod(m_worker, &AudioWorker::resetPeers, Qt::QueuedConnection);
        });
    connect(conf, &SignalingClient::participantLeft, this, [this](qint64 id) {
        QMetaObject::invokeMethod(m_worker, [this, id] { m_worker->dropPeer(id); },
                                  Qt::QueuedConnection);
        });

    // ---- настройки на лету ----
    // Смена устройств перезапускает соответствующую сторону, битрейт Opus
    // меняется без пересоздания кодера, громкость и чувствительность — просто
    // числа в воркере. Устройства выбираем ЗДЕСЬ: QMediaDevices работает только
    // на GUI-потоке, воркеру они уезжают готовыми значениями.
    connect(settings, &MediaSettings::micIdChanged, this, &AudioEngine::pushDevices);
    connect(settings, &MediaSettings::outIdChanged, this, &AudioEngine::pushDevices);
    connect(settings, &MediaSettings::audioQualityChanged, this, [this] {
        const int bps = m_settings->audioBitrate();
        QMetaObject::invokeMethod(m_worker, [this, bps] { m_worker->setBitrate(bps); },
                                  Qt::QueuedConnection);
        });
    connect(settings, &MediaSettings::volumeChanged, this, &AudioEngine::pushGains);
    connect(settings, &MediaSettings::sensitivityChanged, this, &AudioEngine::pushGains);
    connect(settings, &MediaSettings::screenVolumeChanged, this, &AudioEngine::pushGains);

    // Звук демонстрации: захватываем, только пока слот демонстрации наш и
    // настройка включена. Судья один — updateScreenAudio().
    connect(conf, &SignalingClient::screenChanged, this, &AudioEngine::updateScreenAudio);
    connect(settings, &MediaSettings::screenAudioChanged, this, &AudioEngine::updateScreenAudio);

    pushDevices();
    pushGains();
    const int bps = m_settings->audioBitrate();
    QMetaObject::invokeMethod(m_worker, [this, bps] { m_worker->setBitrate(bps); },
                              Qt::QueuedConnection);
}

AudioEngine::~AudioEngine() {
    // Устройства закрываются на своём потоке: разрушать QAudioSink из чужого
    // нельзя. Блокирующий вызов безопасен — поток крутит свой цикл событий.
    QMetaObject::invokeMethod(m_worker, &AudioWorker::shutdown, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();               // finished -> deleteLater воркера
}

qint64 AudioEngine::playheadMs(quint32 id) const { return m_worker->playheadMs(id); }

void AudioEngine::setOutputMuted(bool muted) {
    if (m_outputMuted == muted) return;
    m_outputMuted = muted;
    QMetaObject::invokeMethod(m_worker, [this, muted] { m_worker->setOutputMuted(muted); },
                              Qt::QueuedConnection);
    emit outputMutedChanged();
}

void AudioEngine::setScreenLive(bool on) {
    if (m_scrLive == on) return;
    m_scrLive = on;
    emit screenAudioLiveChanged();
}

void AudioEngine::onPhase() {
    m_live = (m_conf->phase() == "live");
    updateCapture();
    updatePlayback();
    updateScreenAudio();
}

void AudioEngine::onLocalState(bool mic, bool /*cam*/) {
    m_micOn = mic;
    updateCapture();
}

void AudioEngine::onLeft() {
    m_live = false;         // фаза осталась "live", но комнаты уже нет
    updateCapture();
    updatePlayback();
    updateScreenAudio();
}

// Захват идёт <=> мы в эфире И микрофон включён. Все дороги ведут сюда.
void AudioEngine::updateCapture() {
    const bool want = m_live && m_micOn;
    QMetaObject::invokeMethod(m_worker, [this, want] { m_worker->setCapture(want); },
                              Qt::QueuedConnection);
}

// Играем <=> мы в эфире.
void AudioEngine::updatePlayback() {
    const bool want = m_live;
    QMetaObject::invokeMethod(m_worker, [this, want] { m_worker->setPlayback(want); },
                              Qt::QueuedConnection);
}

// Захват звука машины идёт <=> мы в эфире, слот демонстрации закреплён за нами
// И настройка включена. Слот спрашиваем у сервера (как и видео экрана): пока он
// не наш, снимать чужой звук незачем.
void AudioEngine::updateScreenAudio() {
    const qint64 me = m_conf->myId();
    const bool sharing = me != 0 && m_conf->screenId() == me;
    const bool want = m_live && sharing && m_settings->screenAudio();
    QMetaObject::invokeMethod(m_worker, [this, want] { m_worker->setScreenAudio(want); },
                              Qt::QueuedConnection);
}

void AudioEngine::pushDevices() {
    const QAudioDevice in = m_settings->audioInput();
    const QAudioDevice out = m_settings->audioOutput();
    QMetaObject::invokeMethod(m_worker, [this, in, out] { m_worker->setDevices(in, out); },
                              Qt::QueuedConnection);
}

void AudioEngine::pushGains() {
    const qreal vol = m_settings->volumeGain();
    const qreal sens = m_settings->sensitivityGain();
    const qreal scr = m_settings->screenVolumeGain();
    QMetaObject::invokeMethod(m_worker, [this, vol, sens, scr] {
        m_worker->setGains(vol, sens, scr);
        }, Qt::QueuedConnection);
}
