#include "AudioEngine.h"
#include "../MediaSettings.h"
#include "../net/SignalingClient.h"
#include "../net/Protocol.h"
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QIODevice>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QtMath>
#include <opus.h>


// Формат конференции — как у веба: 48 кГц, моно, Int16.

static const int kRate = 48000;
static const int kFrameSamples = 960;                 // 20 мс при 48 кГц
static const int kFrameBytes = kFrameSamples * 2;   // Int16 = 2 байта/сэмпл

static QAudioFormat confFormat() {
    QAudioFormat f;
    f.setSampleRate(kRate);
    f.setChannelCount(1);
    f.setSampleFormat(QAudioFormat::Int16);
    return f;
}

AudioEngine::AudioEngine(SignalingClient* conf, MediaSettings* settings, QObject* parent)
    : QObject(parent), m_conf(conf), m_settings(settings)
{
    connect(conf, &SignalingClient::phaseChanged, this, &AudioEngine::onPhase);
    connect(conf, &SignalingClient::localStateChanged, this, &AudioEngine::onLocalState);
    connect(conf, &SignalingClient::left, this, &AudioEngine::onLeft);

    connect(conf, &SignalingClient::binaryFrame, this, &AudioEngine::onBinaryFrame);
    connect(conf, &SignalingClient::joinOk, this, &AudioEngine::onJoinOk);
    connect(conf, &SignalingClient::participantLeft, this, &AudioEngine::onParticipantLeft);

    // Настройки на лету: смена устройств перезапускает соответствующую сторону,
    // битрейт Opus меняется без пересоздания кодера. Громкость/чувствительность
    // читаются по месту — на каждый кадр, сигналов не нужно.
    connect(settings, &MediaSettings::micIdChanged, this, &AudioEngine::restartCapture);
    connect(settings, &MediaSettings::outIdChanged, this, &AudioEngine::restartPlayback);
    connect(settings, &MediaSettings::audioQualityChanged, this, [this]() {
        if (m_enc) opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(m_settings->audioBitrate()));
        });

    // Насос вывода: 10 мс, точный таймер (обычный на Windows может «плавать» до 15 мс).
    m_pumpTimer = new QTimer(this);
    m_pumpTimer->setTimerType(Qt::PreciseTimer);
    m_pumpTimer->setInterval(10);
    connect(m_pumpTimer, &QTimer::timeout, this, &AudioEngine::pump);
}

AudioEngine::~AudioEngine() { stopCapture(); stopPlayback(); }

void AudioEngine::onPhase() {
    m_live = (m_conf->phase() == "live");
    updateCapture();
    updatePlayback();
}

void AudioEngine::onLocalState(bool mic, bool /*cam*/) {
    m_micOn = mic;
    updateCapture();
}

void AudioEngine::onLeft() {
    m_live = false;         // фаза осталась "live", но комнаты уже нет
    updateCapture();
    updatePlayback();
}

// Захват идёт <=> мы в эфире И микрофон включён. Все дороги ведут сюда.
void AudioEngine::updateCapture() {
    const bool want = m_live && m_micOn;
    if (want && !m_source)      startCapture();
    else if (!want && m_source) stopCapture();
}

void AudioEngine::startCapture() {
    const QAudioDevice dev = m_settings->audioInput();
    if (dev.isNull()) { qWarning() << "AudioEngine: микрофон не найден"; return; }

    const QAudioFormat fmt = confFormat();
    if (!dev.isFormatSupported(fmt)) {
        // Windows в shared-режиме почти всегда умеет 48к/моно/Int16. Если нет —
        // честный warning; ресемплинг — не наша тема.
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
    opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(m_settings->audioBitrate()));

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
    m_settings->reportMicLevel(0);   // индикатор в настройках гаснет
}

// Смена микрофона в настройках: перезапуск захвата (если он вообще шёл).
void AudioEngine::restartCapture() {
    if (!m_source) return;
    stopCapture();
    updateCapture();
}

// Смена динамиков: перезапуск воспроизведения. Декодеры и буферы участников
// живут — потеря устройства не повод терять состояние Opus-декодеров.
void AudioEngine::restartPlayback() {
    if (!m_sink) return;
    m_pumpTimer->stop();
    m_sink->stop();
    m_sink->deleteLater();
    m_sink = nullptr;
    m_out = nullptr;
    startPlayback();
}

// Порция сэмплов от микрофона: копим и отрезаем ровно по кадру.
void AudioEngine::onCaptured() {
    if (!m_mic || !m_enc) return;
    m_pcm += m_mic->readAll();

    // Чувствительность из настроек (0..2) — гейн до кодека, чтобы у
    // собеседников голос стал реально тише/громче (как у веба).
    const qreal gain = m_settings->sensitivityGain();

    unsigned char packet[1500];   // Opus при 32 кбит/с даёт ~80 байт, запас велик
    while (m_pcm.size() >= kFrameBytes) {
        opus_int16* samples = reinterpret_cast<opus_int16*>(m_pcm.data());
        double sumSq = 0;
        for (int i = 0; i < kFrameSamples; ++i) {
            const qint32 v = qBound<qint32>(-32768, qint32(samples[i] * gain), 32767);
            samples[i] = opus_int16(v);
            sumSq += double(v) * v;
        }
        // RMS по нормализованным сэмплам — индикатор уровня в настройках.
        m_settings->reportMicLevel(qSqrt(sumSq / kFrameSamples) / 32768.0);

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

// Играем <=> мы в эфире.
void AudioEngine::updatePlayback() {
    if (m_live && !m_sink)      startPlayback();
    else if (!m_live && m_sink) stopPlayback();
}

void AudioEngine::startPlayback() {
    const QAudioDevice dev = m_settings->audioOutput();
    if (dev.isNull()) { qWarning() << "AudioEngine: устройств вывода нет"; return; }

    m_sink = new QAudioSink(dev, confFormat(), this);
    // Потолок задержки вывода ~200 мс (10 кадров). Больше — эхо-канал
    // разговора «на секунду позже»; меньше — хрупко к загрузке системы.
    m_sink->setBufferSize(kFrameBytes * 10);
    m_out = m_sink->start();          // push-режим: мы пишем, синк играет
    m_pumpTimer->start();
}

void AudioEngine::stopPlayback() {
    m_pumpTimer->stop();
    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
        m_out = nullptr;
    }
    resetPeers();
}

void AudioEngine::resetPeers() {
    for (Peer& p : m_peers)
        if (p.dec) opus_decoder_destroy(p.dec);
    m_peers.clear();
}

// Каждый join_ok — в т.ч. РЕКОННЕКТ: всё накопленное до обрыва — мусор,
// а у анонимов после реконнекта ещё и все id новые (§2.3 гайда).
void AudioEngine::onJoinOk() { resetPeers(); }

void AudioEngine::onParticipantLeft(qint64 id) {
    auto it = m_peers.find(quint32(id));
    if (it != m_peers.end()) {
        if (it->dec) opus_decoder_destroy(it->dec);
        m_peers.erase(it);
    }
}

void AudioEngine::onBinaryFrame(const QByteArray& d) {
    Proto::FrameV2 f;
    if (!Proto::unpack(d, f)) return;                 // мусор короче заголовка

    if (f.type != Proto::AUDIO_CODED) return;         // видео/экран — M3, M7
    if (f.codec != Proto::CODEC_OPUS) return;         // незнакомый кодек — молча мимо
    if (f.flags & Proto::FLAG_ENCRYPTED) return;      // E2E (M5): тишина честнее каши
    if (!m_out) return;                               // не играем — не тратим CPU

    Peer& p = m_peers[f.sender];                      // operator[] создаст пустого
    if (!p.dec) {
        int err = 0;
        p.dec = opus_decoder_create(kRate, 1, &err);
        if (err != OPUS_OK) { m_peers.remove(f.sender); return; }
    }

    QByteArray chunk(kFrameBytes, 0);
    const int samples = opus_decode(p.dec,
        reinterpret_cast<const unsigned char*>(f.payload.constData()),
        f.payload.size(),
        reinterpret_cast<opus_int16*>(chunk.data()), kFrameSamples, 0);
    if (samples != kFrameSamples) return;             // битый/нестандартный кадр

    p.queue.append(chunk);
    if (p.queue.size() > 12)                          // лаг раздулся — срезаем
        while (p.queue.size() > 6)
            p.queue.removeFirst();
}

// Каждые 10 мс: пока синк готов взять целый кадр — отдаём кадр микса.
void AudioEngine::pump() {
    if (!m_sink || !m_out) return;
    while (m_sink->bytesFree() >= kFrameBytes)
        m_out->write(mixOneFrame());
}

// Один кадр (960 сэмплов) — сумма всех «играющих» участников с клампом.
// Никто не играет — кадр тишины (нули): динамики любят непрерывность.
QByteArray AudioEngine::mixOneFrame() {
    qint32 acc[kFrameSamples] = {};                   // 32 бита: сумма не переполнится

    for (Peer& p : m_peers) {
        if (!p.playing) {
            if (p.queue.size() >= 3) p.playing = true;    // предбуфер ~60 мс набран
            else continue;                                 // ещё копит — молчит
        }
        if (p.queue.isEmpty()) {                           // сеть икнула сильнее запаса
            p.playing = false;                             // уходим добуферизоваться
            continue;
        }
        const QByteArray chunk = p.queue.takeFirst();
        const qint16* s = reinterpret_cast<const qint16*>(chunk.constData());
        for (int i = 0; i < kFrameSamples; ++i)
            acc[i] += s[i];                                // микс = сложение волн
    }

    // Громкость воспроизведения из настроек (0..2) — на итоговый микс.
    const qreal vol = m_settings->volumeGain();

    QByteArray out(kFrameBytes, 0);
    qint16* o = reinterpret_cast<qint16*>(out.data());
    for (int i = 0; i < kFrameSamples; ++i)
        o[i] = qint16(qBound<qint32>(-32768, qint32(acc[i] * vol), 32767));  // кламп в Int16
    return out;
}