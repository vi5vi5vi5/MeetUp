#include "AudioEngine.h"
#include "ScreenAudioCapture.h"
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
static const int kFrameMs = 20;                     // длительность кадра
// Сколько звука держим в QAudioSink. Наливать его «до упора» нельзя: полный
// буфер — это чистая задержка, разговор уезжает на четверть секунды.
static const int kSinkTargetFrames = 3;             // ~60 мс

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

    // Звук демонстрации: захватываем, только пока слот демонстрации наш и
    // настройка включена. Судья один — updateScreenAudio().
    m_scrCapture = new ScreenAudioCapture(this);
    connect(m_scrCapture, &ScreenAudioCapture::pcmFrame, this, &AudioEngine::onScreenPcm);
    connect(m_scrCapture, &ScreenAudioCapture::failed, this, [this](const QString& t) {
        // Поток уже погас сам, поэтому прибираем за ним здесь: иначе кодер
        // остался бы висеть, а updateScreenAudio ничего бы не сделал —
        // захват уже неактивен, и ветка остановки до него не доходит.
        stopScreenAudio();
        m_settings->setScreenAudio(false);      // не оставляем тумблер обманкой
        emit screenAudioError(t);
        });
    connect(conf, &SignalingClient::screenChanged, this, &AudioEngine::updateScreenAudio);
    connect(settings, &MediaSettings::screenAudioChanged, this, &AudioEngine::updateScreenAudio);

    // Насос вывода: 10 мс, точный таймер (обычный на Windows может «плавать» до 15 мс).
    m_pumpTimer = new QTimer(this);
    m_pumpTimer->setTimerType(Qt::PreciseTimer);
    m_pumpTimer->setInterval(10);
    connect(m_pumpTimer, &QTimer::timeout, this, &AudioEngine::pump);
}

AudioEngine::~AudioEngine() { stopCapture(); stopScreenAudio(); stopPlayback(); }

void AudioEngine::setOutputMuted(bool muted) {
    if (m_outputMuted == muted) return;
    m_outputMuted = muted;
    emit outputMutedChanged();
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
    m_audioClockMs = 0;           // часы меток начнутся с первой пачки
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
    m_audioClockMs = 0;
    m_settings->reportMicLevel(0);   // индикатор в настройках гаснет
}

// Сколько миллисекунд звука ещё не проиграно из QAudioSink.
int AudioEngine::sinkQueuedMs() const {
    if (!m_sink) return 0;
    const int queued = m_sink->bufferSize() - m_sink->bytesFree();
    return queued > 0 ? queued / kFrameBytes * kFrameMs : 0;
}

qint64 AudioEngine::playheadMs(quint32 id) const {
    auto it = m_peers.constFind(id);
    if (it == m_peers.constEnd() || it->lastTs == 0) return 0;
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - it->lastTsAt;
    if (age > 700) return 0;      // звук иссяк (мик выключили) — не держим видео
    // Всё, что лежит в очереди и в синке, ещё НЕ прозвучало — значит слышимый
    // сейчас звук соответствует более ранней метке отправителя.
    const qint64 bufMs = qint64(it->queue.size()) * kFrameMs + sinkQueuedMs();
    return it->lastTs - bufMs + age;
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

    // Метки времени. Устройство отдаёт сэмплы пачками, и если проштамповать
    // всю пачку одним «сейчас», у приёмника поедут аудио-часы — а по ним он
    // синхронизирует губы. Поэтому ведём монотонные часы (+20 мс на кадр) и
    // подтягиваем их к стенным, только если разошлись (пауза, смена устройства).
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int pending = int(m_pcm.size() / kFrameBytes);
    if (pending > 0) {
        const qint64 batchStart = nowMs - qint64(pending - 1) * kFrameMs;
        if (m_audioClockMs == 0 || qAbs(batchStart - m_audioClockMs) > 200)
            m_audioClockMs = batchStart;
    }

    unsigned char packet[1500];   // Opus при 32 кбит/с даёт ~80 байт, запас велик
    while (m_pcm.size() >= kFrameBytes) {
        opus_int16* samples = reinterpret_cast<opus_int16*>(m_pcm.data());
        double sumSq = 0;
        for (int i = 0; i < kFrameSamples; ++i) {
            const qint32 v = qBound<qint32>(-32768, qint32(samples[i] * gain), 32767);
            samples[i] = opus_int16(v);
            sumSq += double(v) * v;
        }
        // RMS по нормализованным сэмплам — индикатор уровня в настройках
        // и подсветка своей плитки (порог 0.02, как в методичке §6.4).
        const qreal level = qSqrt(sumSq / kFrameSamples) / 32768.0;
        m_settings->reportMicLevel(level);
        if (level > 0.02) m_conf->markSelfSpeaking();

        const int bytes = opus_encode(m_enc, samples, kFrameSamples,
            packet, int(sizeof(packet)));
        m_pcm.remove(0, kFrameBytes);
        if (bytes <= 0) { qWarning() << "AudioEngine: opus_encode =" << bytes; continue; }

        // Заголовок: тип AUDIO_CODED, без флагов, кодек OPUS, часы — мс эпохи
        // (общая шкала аудио/видео — §5.3, не выдумывать свою!).
        m_conf->sendBinary(Proto::pack(
            Proto::AUDIO_CODED, 0, Proto::CODEC_OPUS,
            quint64(m_audioClockMs),
            QByteArray(reinterpret_cast<const char*>(packet), bytes)));
        m_audioClockMs += kFrameMs;   // следующий кадр — ровно на 20 мс позже
    }
}

// ---------- звук демонстрации ----------

// Захват идёт <=> мы в эфире, слот демонстрации закреплён за нами И настройка
// включена. Слот спрашиваем у сервера (как и видео экрана): пока он не наш,
// снимать чужой звук незачем.
void AudioEngine::updateScreenAudio() {
    if (!m_scrCapture) return;
    const qint64 me = m_conf->myId();
    const bool sharing = me != 0 && m_conf->screenId() == me;
    const bool want = m_live && sharing && m_settings->screenAudio();
    if (want && !m_scrCapture->active())        startScreenAudio();
    else if (!want && m_scrCapture->active())   stopScreenAudio();
}

void AudioEngine::startScreenAudio() {
    int err = 0;
    // Не VOIP: здесь музыка и видео, а не речь. OPUS_APPLICATION_AUDIO с
    // сигналом «музыка» и вдвое большим битрейтом — иначе фонограмма
    // превращается в кашу, ради которой всё и затевалось.
    m_scrEnc = opus_encoder_create(kRate, 1, OPUS_APPLICATION_AUDIO, &err);
    if (err != OPUS_OK || !m_scrEnc) {
        qWarning() << "AudioEngine: opus_encoder_create (экран) =" << err;
        m_scrEnc = nullptr;
        return;
    }
    opus_encoder_ctl(m_scrEnc, OPUS_SET_BITRATE(64000));
    opus_encoder_ctl(m_scrEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    m_scrClockMs = 0;
    m_scrCapture->start();
}

void AudioEngine::stopScreenAudio() {
    if (m_scrCapture) m_scrCapture->stop();
    if (m_scrEnc) {
        opus_encoder_destroy(m_scrEnc);
        m_scrEnc = nullptr;
    }
    m_scrClockMs = 0;
}

// Готовый кадр 20 мс от захвата (он уже свёл стерео в моно и нарезал по 960).
void AudioEngine::onScreenPcm(const QByteArray& pcm, qint64 wallMs) {
    if (!m_scrEnc || pcm.size() != kFrameBytes) return;

    QByteArray frame = pcm;                   // копия: гейн правит сэмплы на месте
    opus_int16* samples = reinterpret_cast<opus_int16*>(frame.data());
    const qreal gain = m_settings->screenAudioGain() / 100.0;
    if (!qFuzzyCompare(gain, 1.0))
        for (int i = 0; i < kFrameSamples; ++i)
            samples[i] = opus_int16(qBound<qint32>(-32768, qint32(samples[i] * gain), 32767));

    // Метки — та же монотонная схема, что у микрофона: кадр ровно на 20 мс
    // позже предыдущего, подтяжка к стенным часам только при расхождении.
    if (m_scrClockMs == 0 || qAbs(wallMs - m_scrClockMs) > 200) m_scrClockMs = wallMs;

    unsigned char packet[1500];
    const int bytes = opus_encode(m_scrEnc, samples, kFrameSamples,
                                  packet, int(sizeof(packet)));
    if (bytes <= 0) { qWarning() << "AudioEngine: opus_encode (экран) =" << bytes; return; }

    m_conf->sendBinary(Proto::pack(
        Proto::SCREEN_AUDIO, 0, Proto::CODEC_OPUS, quint64(m_scrClockMs),
        QByteArray(reinterpret_cast<const char*>(packet), bytes)));
    m_scrClockMs += kFrameMs;
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
    for (Peer& p : m_scrPeers)
        if (p.dec) opus_decoder_destroy(p.dec);
    m_scrPeers.clear();
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
    auto sit = m_scrPeers.find(quint32(id));
    if (sit != m_scrPeers.end()) {
        if (sit->dec) opus_decoder_destroy(sit->dec);
        m_scrPeers.erase(sit);
    }
}

void AudioEngine::onBinaryFrame(const QByteArray& d) {
    Proto::FrameV2 f;
    if (!Proto::unpack(d, f)) return;                 // мусор короче заголовка

    // Две звуковые полосы: голос участника и звук его демонстрации.
    const bool isScreen = (f.type == Proto::SCREEN_AUDIO);
    if (f.type != Proto::AUDIO_CODED && !isScreen) return;   // видео — M3, M7
    if (f.codec != Proto::CODEC_OPUS) return;         // незнакомый кодек — молча мимо
    if (f.flags & Proto::FLAG_ENCRYPTED) return;      // E2E (M5): тишина честнее каши
    if (!m_out) return;                               // не играем — не тратим CPU

    QHash<quint32, Peer>& peers = isScreen ? m_scrPeers : m_peers;
    Peer& p = peers[f.sender];                        // operator[] создаст пустого
    if (!p.dec) {
        int err = 0;
        p.dec = opus_decoder_create(kRate, 1, &err);
        if (err != OPUS_OK) { peers.remove(f.sender); return; }
    }

    QByteArray chunk(kFrameBytes, 0);
    const int samples = opus_decode(p.dec,
        reinterpret_cast<const unsigned char*>(f.payload.constData()),
        f.payload.size(),
        reinterpret_cast<opus_int16*>(chunk.data()), kFrameSamples, 0);
    if (samples != kFrameSamples) return;             // битый/нестандартный кадр

    // Подсветка говорящего: RMS по кадру (каждый 16-й сэмпл — достаточно).
    // Только для голоса: громкая музыка из демонстрации не должна зажигать
    // рамку «говорит» вокруг плитки ведущего.
    if (!isScreen) {
        const qint16* s = reinterpret_cast<const qint16*>(chunk.constData());
        double sum = 0; int n = 0;
        for (int i = 0; i < kFrameSamples; i += 16) {
            const double v = s[i] / 32768.0;
            sum += v * v; ++n;
        }
        if (n && qSqrt(sum / n) > 0.02) m_conf->markSpeaking(qint64(f.sender));
    }

    // Часы звука этого участника — по ним VideoEngine придержит обогнавшее видео.
    p.lastTs = qint64(f.ts);
    p.lastTsAt = QDateTime::currentMSecsSinceEpoch();

    p.queue.append(chunk);
    if (p.queue.size() > 12)                          // лаг раздулся — срезаем
        while (p.queue.size() > 6)
            p.queue.removeFirst();
}

// Каждые 10 мс: пока синк готов взять целый кадр — отдаём кадр микса.
void AudioEngine::pump() {
    if (!m_sink || !m_out) return;
    // Доливаем не «сколько влезет», а до цели ~60 мс: всё, что лежит в синке
    // сверх этого, — задержка, которую слышно как «собеседник отвечает позже».
    const int target = kFrameBytes * kSinkTargetFrames;
    while (m_sink->bytesFree() >= kFrameBytes
           && (m_sink->bufferSize() - m_sink->bytesFree()) < target)
        m_out->write(mixOneFrame());
}

// Один кадр (960 сэмплов) — сумма всех «играющих» участников с клампом.
// Никто не играет — кадр тишины (нули): динамики любят непрерывность.
QByteArray AudioEngine::mixOneFrame() {
    qint32 acc[kFrameSamples] = {};                   // 32 бита: сумма не переполнится

    // Голоса и звук демонстраций подмешиваются одинаково — разница только в
    // том, что у второй полосы свои декодеры и свои буферы.
    const auto mixInto = [&acc](QHash<quint32, AudioEngine::Peer>& peers) {
        for (Peer& p : peers) {
            if (!p.playing) {
                if (p.queue.size() >= 3) p.playing = true; // предбуфер ~60 мс набран
                else continue;                             // ещё копит — молчит
            }
            if (p.queue.isEmpty()) {                       // сеть икнула сильнее запаса
                p.playing = false;                         // уходим добуферизоваться
                continue;
            }
            const QByteArray chunk = p.queue.takeFirst();
            const qint16* s = reinterpret_cast<const qint16*>(chunk.constData());
            for (int i = 0; i < kFrameSamples; ++i)
                acc[i] += s[i];                            // микс = сложение волн
        }
    };
    mixInto(m_peers);
    mixInto(m_scrPeers);

    // Громкость воспроизведения из настроек (0..2) — на итоговый микс.
    // «Общий звук» выключен — выход в ноль, но очереди выше мы уже вычерпали:
    // декодеры, часы звука и подсветка «говорит» продолжают жить.
    const qreal vol = m_outputMuted ? 0.0 : m_settings->volumeGain();

    QByteArray out(kFrameBytes, 0);
    qint16* o = reinterpret_cast<qint16*>(out.data());
    for (int i = 0; i < kFrameSamples; ++i)
        o[i] = qint16(qBound<qint32>(-32768, qint32(acc[i] * vol), 32767));  // кламп в Int16
    return out;
}