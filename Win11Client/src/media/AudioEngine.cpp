#include "AudioEngine.h"
#include "../net/SignalingClient.h"
#include "../net/Protocol.h"
#include <QAudioSource>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QIODevice>
#include <QDateTime>
#include <QDebug>
#include <opus.h>

// Формат конференции — как у веба: 48 кГц, моно, Int16.

static const int kRate = 48000;
static const int kFrameSamples = 960;                 // 20 мс при 48 кГц
static const int kFrameBytes = kFrameSamples * 2;   // Int16 = 2 байта/сэмпл
static const int kBitrate = 32000;               // пресет med веба

static QAudioFormat confFormat() {
    QAudioFormat f;
    f.setSampleRate(kRate);
    f.setChannelCount(1);
    f.setSampleFormat(QAudioFormat::Int16);
    return f;
}

AudioEngine::AudioEngine(SignalingClient* conf, QObject* parent)
    : QObject(parent), m_conf(conf)
{
    connect(conf, &SignalingClient::phaseChanged, this, &AudioEngine::onPhase);
    connect(conf, &SignalingClient::localStateChanged, this, &AudioEngine::onLocalState);
    connect(conf, &SignalingClient::left, this, &AudioEngine::onLeft);
}

AudioEngine::~AudioEngine() { stopCapture(); }

void AudioEngine::onPhase() {
    m_live = (m_conf->phase() == "live");
    updateCapture();
}

void AudioEngine::onLocalState(bool mic, bool /*cam*/) {
    m_micOn = mic;
    updateCapture();
}

void AudioEngine::onLeft() {
    m_live = false;         // фаза осталась "live", но комнаты уже нет
    updateCapture();
}

// Захват идёт <=> мы в эфире И микрофон включён. Все дороги ведут сюда.
void AudioEngine::updateCapture() {
    const bool want = m_live && m_micOn;
    if (want && !m_source)      startCapture();
    else if (!want && m_source) stopCapture();
}

void AudioEngine::startCapture() {
    const QAudioDevice dev = QMediaDevices::defaultAudioInput();
    if (dev.isNull()) { qWarning() << "AudioEngine: микрофон не найден"; return; }

    const QAudioFormat fmt = confFormat();
    if (!dev.isFormatSupported(fmt)) {
        // Windows в shared-режиме почти всегда умеет 48к/моно/Int16. Если нет —
        // честный warning; ресемплинг и выбор устройства — тема M8.
        qWarning() << "AudioEngine: устройство не умеет 48 кГц/моно/Int16:"
            << dev.description();
        return;
    }

    int err = 0;
    m_enc = opus_encoder_create(kRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) {
        qWarning() << "AudioEngine: opus_encoder_create =" << err;
        m_enc = nullptr;
        return;
    }
    opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(kBitrate));

    m_pcm.clear();
    m_source = new QAudioSource(dev, fmt, this);
    m_mic = m_source->start();    // поехали: source пишет, мы читаем
    connect(m_mic, &QIODevice::readyRead, this, &AudioEngine::onCaptured);
}

void AudioEngine::stopCapture() {
    if (m_source) {
        m_source->stop();
        m_source->deleteLater();  // удалит и свой QIODevice (m_mic)
        m_source = nullptr;
        m_mic = nullptr;
    }
    if (m_enc) {
        opus_encoder_destroy(m_enc);
        m_enc = nullptr;
    }
    m_pcm.clear();                // недособранный хвост кадра — в мусор
}

// Порция сэмплов от микрофона: копим и отрезаем ровно по кадру.
void AudioEngine::onCaptured() {
    if (!m_mic || !m_enc) return;
    m_pcm += m_mic->readAll();

    unsigned char packet[1500];   // Opus при 32 кбит/с даёт ~80 байт, запас велик
    while (m_pcm.size() >= kFrameBytes) {
        const opus_int16* samples =
            reinterpret_cast<const opus_int16*>(m_pcm.constData());
        const int bytes = opus_encode(m_enc, samples, kFrameSamples,
            packet, int(sizeof(packet)));
        m_pcm.remove(0, kFrameBytes);
        if (bytes <= 0) { qWarning() << "AudioEngine: opus_encode =" << bytes; continue; }

        // Заголовок: тип AUDIO_CODED, без флагов, кодек OPUS, часы — мс эпохи
        // (общая шкала аудио/видео — §5.3, не выдумывать свою!).
        m_conf->sendBinary(Proto::pack(
            Proto::AUDIO_CODED, 0, Proto::CODEC_OPUS,
            quint64(QDateTime::currentMSecsSinceEpoch()),
            QByteArray(reinterpret_cast<const char*>(packet), bytes)));
    }
}