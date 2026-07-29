#include "AudioWorker.h"
#include "ScreenAudioCapture.h"
#include "../net/Protocol.h"
#include "../crypto/E2eCipher.h"
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QtMath>
#include <opus.h>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <objbase.h>     // CoInitializeEx: WIN32_LEAN_AND_MEAN его отрезает
#endif

// Формат конференции — как у веба: 48 кГц, моно, Int16.

static const int kRate = 48000;
static const int kFrameSamples = 960;               // 20 мс при 48 кГц
static const int kFrameBytes = kFrameSamples * 2;   // Int16 = 2 байта/сэмпл
static const int kFrameMs = 20;                     // длительность кадра
// Сколько звука держим в QAudioSink. Наливать его «до упора» нельзя: полный
// буфер — это чистая задержка, разговор уезжает на четверть секунды.
static const int kSinkTargetFrames = 3;             // ~60 мс

// Джиттер-буфер. Предбуфер начинается с минимума и растёт только там, где
// реально рвётся: у собеседника, чей клиент тормозит, или на плохой сети.
static const int kPrebufMin = 3;        // 60 мс — как было всегда
static const int kPrebufMax = 8;        // 160 мс — потолок разумной задержки
static const int kPlcMaxRun = 5;        // до 100 мс подряд досочиняем кадры
static const qint64 kCalmMs = 5000;     // столько играем ровно — и ужимаем запас

static QAudioFormat confFormat() {
    QAudioFormat f;
    f.setSampleRate(kRate);
    f.setChannelCount(1);
    f.setSampleFormat(QAudioFormat::Int16);
    return f;
}

AudioWorker::AudioWorker(E2eCipher* cipher, QObject* parent)
    : QObject(parent), m_cipher(cipher) {}

AudioWorker::~AudioWorker() { shutdown(); }

// Всё создаётся здесь, а не в конструкторе: конструктор выполняется на
// GUI-потоке (объект ещё не переехал), а таймеры и звуковые устройства должны
// принадлежать своему потоку с рождения.
void AudioWorker::init() {
#ifdef Q_OS_WIN
    // COM для звуковых устройств. Именно STA, хотя захват звука приложений
    // рядом живёт в MTA: там мы вызываем WASAPI сами, а здесь его вызывает Qt —
    // и его QComHelper поднимает STA. Под MTA он получал бы RPC_E_CHANGED_MODE и
    // писал в лог «Failed to initialize COM library» на каждый старт устройства
    // (проверено пробой scratchpad/audioprobe: звук идёт при любом варианте,
    // молча — только при этом). Квартира держится всю жизнь потока, вложенные
    // CoInitializeEx самого Qt в неё просто складываются.
    m_comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
#endif

    // Насос вывода: 10 мс, точный таймер (обычный на Windows может «плавать»
    // до 15 мс). Здесь, в отличие от GUI-потока, его никто не задерживает.
    m_pumpTimer = new QTimer(this);
    m_pumpTimer->setTimerType(Qt::PreciseTimer);
    m_pumpTimer->setInterval(10);
    connect(m_pumpTimer, &QTimer::timeout, this, &AudioWorker::pump);

    // Сторож второй полосы: ведущий мог выключить звук демонстрации или уйти,
    // и тогда ручка громкости на сцене должна исчезнуть. Тикает только пока
    // звук идёт — в тишине таймера нет.
    m_scrLiveTimer = new QTimer(this);
    m_scrLiveTimer->setInterval(1000);
    connect(m_scrLiveTimer, &QTimer::timeout, this, [this] {
        if (QDateTime::currentMSecsSinceEpoch() - m_scrRxAt > 2000) setScreenLive(false);
        });

    m_scrCapture = new ScreenAudioCapture(this);
    connect(m_scrCapture, &ScreenAudioCapture::pcmFrame, this, &AudioWorker::onScreenPcm);
    connect(m_scrCapture, &ScreenAudioCapture::failed, this, [this](const QString& t) {
        // Поток уже погас сам, поэтому прибираем за ним здесь: иначе кодер
        // остался бы висеть. Тумблер в настройках снимает AudioEngine — он
        // единственный, кому можно трогать настройки.
        stopScreenAudio();
        emit screenFailed(t);
        });
}

// Зовётся дважды: явно перед остановкой потока и ещё раз из деструктора
// (воркер умирает уже после выхода из цикла событий). Поэтому идемпотентно —
// лишний CoUninitialize снял бы чужую ссылку на COM.
void AudioWorker::shutdown() {
    stopCapture();
    stopScreenAudio();
    stopPlayback();
#ifdef Q_OS_WIN
    if (m_comReady) { CoUninitialize(); m_comReady = false; }
#endif
}

// ---------- настройки снаружи ----------

void AudioWorker::setDevices(const QAudioDevice& input, const QAudioDevice& output) {
    if (m_inDev != input) {
        m_inDev = input;
        if (m_source) stopCapture();                    // перезапуск на ходу
        // Не «если шло», а «если должно идти»: устройство могло не открыться в
        // прошлый раз (наушники ещё не воткнули), и тогда именно смена выбора —
        // повод попробовать снова.
        if (m_wantCapture && !m_source) startCapture();
    }
    if (m_outDev != output) {
        m_outDev = output;
        if (m_sink) {
            // Декодеры и буферы участников живут: потеря устройства не повод
            // терять состояние Opus-декодеров.
            m_pumpTimer->stop();
            m_sink->stop();
            m_sink->deleteLater();
            m_sink = nullptr;
            m_out = nullptr;
        }
        if (m_wantPlayback && !m_sink) startPlayback();
    }
}

void AudioWorker::setCapture(bool on) {
    m_wantCapture = on;
    if (on && !m_source)       startCapture();
    else if (!on && m_source)  stopCapture();
}

void AudioWorker::setPlayback(bool on) {
    m_wantPlayback = on;
    if (on && !m_sink)       startPlayback();
    else if (!on && m_sink)  stopPlayback();
}

void AudioWorker::setScreenAudio(bool on) {
    m_wantScreenAudio = on;
    if (!m_scrCapture) return;
    if (on && !m_scrCapture->active())       startScreenAudio();
    else if (!on && m_scrCapture->active())  stopScreenAudio();
}

void AudioWorker::setBitrate(int bps) {
    m_bitrate = bps;
    if (m_enc) opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(bps));
}

void AudioWorker::setGains(qreal volume, qreal sensitivity, qreal screenVolume) {
    m_volGain = volume;
    m_sensGain = sensitivity;
    m_scrVolGain = screenVolume;
}

void AudioWorker::setOutputMuted(bool muted) { m_outputMuted = muted; }

// ---------- захват микрофона ----------

void AudioWorker::startCapture() {
    if (m_inDev.isNull()) { qWarning() << "AudioWorker: микрофон не найден"; return; }

    const QAudioFormat fmt = confFormat();
    if (!m_inDev.isFormatSupported(fmt)) {
        // Windows в shared-режиме почти всегда умеет 48к/моно/Int16. Если нет —
        // честный warning; ресемплинг — не наша тема.
        qWarning() << "AudioWorker: устройство не умеет 48 кГц/моно/Int16:"
                   << m_inDev.description();
        return;
    }

    int err = 0;
    m_enc = opus_encoder_create(kRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) {
        qWarning() << "AudioWorker: opus_encoder_create =" << err;
        m_enc = nullptr;
        return;
    }
    opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(m_bitrate));

    m_pcm.clear();
    m_audioClockMs = 0;           // часы меток начнутся с первой пачки
    m_source = new QAudioSource(m_inDev, fmt, this);
    m_mic = m_source->start();    // поехали: source пишет, мы читаем
    connect(m_mic, &QIODevice::readyRead, this, &AudioWorker::onCaptured);
}

void AudioWorker::stopCapture() {
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
    m_micLevelAt = 0;
    emit micLevel(0);             // индикатор в настройках гаснет
}

// Порция сэмплов от микрофона: копим и отрезаем ровно по кадру.
void AudioWorker::onCaptured() {
    if (!m_mic || !m_enc) return;
    m_pcm += m_mic->readAll();

    // Чувствительность из настроек (0..2) — гейн до кодека, чтобы у
    // собеседников голос стал реально тише/громче (как у веба).
    const qreal gain = m_sensGain;

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
        // Оба адресата живут на GUI-потоке, поэтому будить его на каждый
        // двадцатимиллисекундный кадр незачем. 60 мс, а не 100: у самих
        // настроек стоит ещё один фильтр в 100 мс, и два одинаковых порога
        // гасили бы через раз — полоска обновлялась бы вдвое реже обещанного.
        const qreal level = qSqrt(sumSq / kFrameSamples) / 32768.0;
        if (nowMs - m_micLevelAt >= 60) { m_micLevelAt = nowMs; emit micLevel(level); }
        if (level > 0.02 && nowMs - m_selfSpokeAt >= 150) {
            m_selfSpokeAt = nowMs;      // подсветка держится 450 мс — хватает
            emit selfSpeaking();
        }

        const int bytes = opus_encode(m_enc, samples, kFrameSamples,
            packet, int(sizeof(packet)));
        m_pcm.remove(0, kFrameBytes);
        if (bytes <= 0) { qWarning() << "AudioWorker: opus_encode =" << bytes; continue; }

        // Шифруется только payload: заголовок сервер обязан читать, чтобы
        // понять, кому релеить полосу. Не смогли запечатать — молчим: отдать
        // голос открытым при включённом шифровании хуже, чем не отдать вовсе.
        QByteArray body(reinterpret_cast<const char*>(packet), bytes);
        quint8 flags = 0;
        if (m_cipher->active()) {
            body = m_cipher->seal(Proto::AUDIO_CODED, Proto::CODEC_OPUS, body);
            if (body.isEmpty()) { m_audioClockMs += kFrameMs; continue; }
            flags = Proto::FLAG_ENCRYPTED;
        }

        // Заголовок: тип AUDIO_CODED, кодек OPUS, часы — мс эпохи
        // (общая шкала аудио/видео — §5.3, не выдумывать свою!).
        const QByteArray frame = Proto::pack(
            Proto::AUDIO_CODED, flags, Proto::CODEC_OPUS,
            quint64(m_audioClockMs), body);
        emit packetReady(frame);
        emit txAudio(false, frame.size());
        m_audioClockMs += kFrameMs;   // следующий кадр — ровно на 20 мс позже
    }
}

// ---------- звук демонстрации ----------

void AudioWorker::startScreenAudio() {
    int err = 0;
    // Не VOIP: здесь музыка и видео, а не речь. OPUS_APPLICATION_AUDIO с
    // сигналом «музыка» и вдвое большим битрейтом — иначе фонограмма
    // превращается в кашу, ради которой всё и затевалось.
    m_scrEnc = opus_encoder_create(kRate, 1, OPUS_APPLICATION_AUDIO, &err);
    if (err != OPUS_OK || !m_scrEnc) {
        qWarning() << "AudioWorker: opus_encoder_create (экран) =" << err;
        m_scrEnc = nullptr;
        return;
    }
    opus_encoder_ctl(m_scrEnc, OPUS_SET_BITRATE(64000));
    opus_encoder_ctl(m_scrEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    m_scrClockMs = 0;
    m_scrCapture->start();
}

void AudioWorker::stopScreenAudio() {
    if (m_scrCapture) m_scrCapture->stop();
    if (m_scrEnc) {
        opus_encoder_destroy(m_scrEnc);
        m_scrEnc = nullptr;
    }
    m_scrClockMs = 0;
}

// Готовый кадр 20 мс от захвата (он уже свёл стерео в моно и нарезал по 960).
void AudioWorker::onScreenPcm(const QByteArray& pcm, qint64 wallMs) {
    if (!m_scrEnc || pcm.size() != kFrameBytes) return;

    // Отправляем как есть, ничего не приглушая: громкость этой полосы крутит
    // КАЖДЫЙ слушатель у себя (screenVolume, см. mixInto). Пока ручка была
    // здесь, ведущий решал за всю комнату сразу — а расслышать за фонограммой
    // разговор хотят не все и не одновременно.
    const opus_int16* samples = reinterpret_cast<const opus_int16*>(pcm.constData());

    // Метки — та же монотонная схема, что у микрофона: кадр ровно на 20 мс
    // позже предыдущего, подтяжка к стенным часам только при расхождении.
    if (m_scrClockMs == 0 || qAbs(wallMs - m_scrClockMs) > 200) m_scrClockMs = wallMs;

    unsigned char packet[1500];
    const int bytes = opus_encode(m_scrEnc, samples, kFrameSamples,
                                  packet, int(sizeof(packet)));
    if (bytes <= 0) { qWarning() << "AudioWorker: opus_encode (экран) =" << bytes; return; }

    QByteArray body(reinterpret_cast<const char*>(packet), bytes);
    quint8 flags = 0;
    if (m_cipher->active()) {                       // см. onCaptured
        body = m_cipher->seal(Proto::SCREEN_AUDIO, Proto::CODEC_OPUS, body);
        if (body.isEmpty()) { m_scrClockMs += kFrameMs; return; }
        flags = Proto::FLAG_ENCRYPTED;
    }

    const QByteArray frame = Proto::pack(
        Proto::SCREEN_AUDIO, flags, Proto::CODEC_OPUS, quint64(m_scrClockMs), body);
    emit packetReady(frame);
    emit txAudio(true, frame.size());   // цена демонстрации — вместе со звуком
    m_scrClockMs += kFrameMs;
}

void AudioWorker::setScreenLive(bool on) {
    if (m_scrLive == on) return;
    m_scrLive = on;
    if (on) m_scrLiveTimer->start();
    else    m_scrLiveTimer->stop();
    emit screenLive(on);
}

// ---------- воспроизведение ----------

void AudioWorker::startPlayback() {
    if (m_outDev.isNull()) { qWarning() << "AudioWorker: устройств вывода нет"; return; }

    m_sink = new QAudioSink(m_outDev, confFormat(), this);
    // Потолок задержки вывода ~200 мс (10 кадров). Больше — эхо-канал
    // разговора «на секунду позже»; меньше — хрупко к загрузке системы.
    m_sink->setBufferSize(kFrameBytes * 10);
    m_out = m_sink->start();          // push-режим: мы пишем, синк играет
    m_pumpTimer->start();
}

void AudioWorker::stopPlayback() {
    if (m_pumpTimer) m_pumpTimer->stop();
    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
        m_out = nullptr;
    }
    resetPeers();
}

void AudioWorker::destroyPeers(QHash<quint32, Peer>& peers) {
    for (auto it = peers.begin(); it != peers.end(); ++it) {
        if (it->dec) opus_decoder_destroy(it->dec);
        // Замок снимаем явно: участник мог уйти запертым, а его плитка в
        // интерфейсе живёт своей жизнью и сама об этом не узнает.
        setLocked(*it, it.key(), false);
    }
    peers.clear();
}

void AudioWorker::setLocked(Peer& p, quint32 sender, bool locked) {
    if (p.locked == locked) return;
    p.locked = locked;
    emit peerLocked(qint64(sender), locked);
}

// Каждый join_ok — в т.ч. РЕКОННЕКТ: всё накопленное до обрыва — мусор,
// а у анонимов после реконнекта ещё и все id новые (§2.3 гайда).
void AudioWorker::resetPeers() {
    destroyPeers(m_peers);
    destroyPeers(m_scrPeers);
    setScreenLive(false);
    QMutexLocker lock(&m_phLock);
    m_playheads.clear();
}

void AudioWorker::dropPeer(qint64 id) {
    const quint32 key = quint32(id);
    auto it = m_peers.find(key);
    if (it != m_peers.end()) {
        if (it->dec) opus_decoder_destroy(it->dec);
        setLocked(*it, key, false);
        m_peers.erase(it);
    }
    auto sit = m_scrPeers.find(key);
    if (sit != m_scrPeers.end()) {
        if (sit->dec) opus_decoder_destroy(sit->dec);
        setLocked(*sit, key, false);
        m_scrPeers.erase(sit);
    }
    forgetPlayhead(key);
}

void AudioWorker::onFrame(const QByteArray& d) {
    Proto::FrameV2 f;
    if (!Proto::unpack(d, f)) return;                 // мусор короче заголовка

    // Две звуковые полосы: голос участника и звук его демонстрации. Всё
    // остальное сюда не доезжает — транспорт развёл полосы по потокам.
    const bool isScreen = (f.type == Proto::SCREEN_AUDIO);
    if (f.type != Proto::AUDIO_CODED && !isScreen) return;
    if (f.codec != Proto::CODEC_OPUS) return;         // незнакомый кодек — молча мимо
    if (!m_out) return;                               // не играем — не тратим CPU

    QHash<quint32, Peer>& peers = isScreen ? m_scrPeers : m_peers;
    Peer& p = peers[f.sender];                        // operator[] создаст пустого

    // Сквозное шифрование. Не открылось — у собеседника другой ключ (или его
    // нет вовсе): три кадра подряд, и об этом говорим наверх, чтобы человек
    // видел причину тишины, а не гадал. Кадр без флага — просто открытый:
    // собеседник вещает без шифрования, и «замок» тут ни при чём.
    if (f.flags & Proto::FLAG_ENCRYPTED) {
        const QByteArray plain = m_cipher->open(f.type, f.codec, f.payload);
        if (plain.isEmpty()) {
            if (++p.cryptoFails >= 3) setLocked(p, f.sender, true);
            return;
        }
        f.payload = plain;
    }
    p.cryptoFails = 0;
    setLocked(p, f.sender, false);

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

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

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
        // Окно подсветки — 450 мс, поэтому дёргать GUI чаще, чем раз в 150 мс,
        // смысла нет (а кадров приходит полсотни в секунду).
        if (n && qSqrt(sum / n) > 0.02 && now - p.lastSpokeAt >= 150) {
            p.lastSpokeAt = now;
            emit speaking(qint64(f.sender));
        }
    }

    // Часы звука этого участника — по ним VideoEngine придержит обогнавшее видео.
    p.lastTs = qint64(f.ts);
    p.lastTsAt = now;
    if (isScreen) { m_scrRxAt = now; setScreenLive(true); }

    p.queue.append(chunk);
    // Лаг раздулся (сеть отдала пачку, отправитель залип) — срезаем, оставляя
    // ровно текущий запас: копить дальше значит разговаривать с задержкой.
    if (p.queue.size() > p.prebuf + 9)
        while (p.queue.size() > p.prebuf + 3)
            p.queue.removeFirst();

    if (!isScreen) publishPlayheads();
}

// ---------- микс и вывод ----------

// Сколько миллисекунд звука ещё не проиграно из QAudioSink.
int AudioWorker::sinkQueuedMs() const {
    if (!m_sink) return 0;
    const int queued = m_sink->bufferSize() - m_sink->bytesFree();
    return queued > 0 ? queued / kFrameBytes * kFrameMs : 0;
}

// Каждые 10 мс: пока синк готов взять целый кадр — отдаём кадр микса.
void AudioWorker::pump() {
    if (!m_sink || !m_out) return;
    // Доливаем не «сколько влезет», а до цели ~60 мс: всё, что лежит в синке
    // сверх этого, — задержка, которую слышно как «собеседник отвечает позже».
    const int target = kFrameBytes * kSinkTargetFrames;
    while (m_sink->bytesFree() >= kFrameBytes
           && (m_sink->bufferSize() - m_sink->bytesFree()) < target)
        m_out->write(mixOneFrame());
    publishPlayheads();
}

// Один кадр (960 сэмплов) — сумма всех «играющих» участников с клампом.
// Никто не играет — кадр тишины (нули): динамики любят непрерывность.
QByteArray AudioWorker::mixOneFrame() {
    qint32 acc[kFrameSamples] = {};                   // 32 бита: сумма не переполнится
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Голоса и звук демонстраций подмешиваются одинаково — разница в том, что у
    // второй полосы свои декодеры, свои буферы и своя громкость: фонограмма
    // может заглушать разговор, и убавляет её тот, кому мешает, а не ведущий.
    mixInto(acc, m_peers, 1.0, now);
    mixInto(acc, m_scrPeers, m_scrVolGain, now);

    // Громкость воспроизведения из настроек (0..2) — на итоговый микс.
    // «Общий звук» выключен — выход в ноль, но очереди выше мы уже вычерпали:
    // декодеры, часы звука и подсветка «говорит» продолжают жить.
    const qreal vol = m_outputMuted ? 0.0 : m_volGain;

    QByteArray out(kFrameBytes, 0);
    qint16* o = reinterpret_cast<qint16*>(out.data());
    for (int i = 0; i < kFrameSamples; ++i)
        o[i] = qint16(qBound<qint32>(-32768, qint32(acc[i] * vol), 32767));  // кламп в Int16
    return out;
}

void AudioWorker::mixInto(qint32* acc, QHash<quint32, Peer>& peers, qreal gain, qint64 now) {
    for (Peer& p : peers) {
        if (!p.playing) {
            if (p.queue.size() >= p.prebuf) {          // предбуфер набран
                p.playing = true;
                p.plcRun = 0;
                p.calmSince = now;
            } else continue;                           // ещё копит — молчит
        }

        QByteArray chunk;
        if (!p.queue.isEmpty()) {
            chunk = p.queue.takeFirst();
            p.plcRun = 0;
            // Давно играем ровно — осторожно возвращаем запас к минимуму:
            // задержка, набранная на одном провале, иначе осталась бы навсегда.
            if (p.prebuf > kPrebufMin && now - p.calmSince > kCalmMs) {
                --p.prebuf;
                p.calmSince = now;
            }
        } else {
            // Очередь пуста. Раньше здесь начиналась тишина и добуферизация —
            // на слух это провал и щелчок. Opus умеет досочинить кадр по
            // предыдущему (packet loss concealment): короткий разрыв становится
            // глухим «пф», которого почти не замечают.
            if (!p.dec || p.plcRun >= kPlcMaxRun || now - p.lastTsAt > 700) {
                p.playing = false;                     // поток и правда кончился
                continue;
            }
            chunk = QByteArray(kFrameBytes, 0);
            const int n = opus_decode(p.dec, nullptr, 0,
                reinterpret_cast<opus_int16*>(chunk.data()), kFrameSamples, 0);
            if (n != kFrameSamples) { p.playing = false; continue; }
            if (++p.plcRun == 1) {
                // Первый провал в серии: значит запаса не хватило. Добавляем —
                // и следующий такой же провал уже никто не услышит.
                p.prebuf = qMin(p.prebuf + 2, kPrebufMax);
                p.calmSince = now;
            }
        }

        const qint16* s = reinterpret_cast<const qint16*>(chunk.constData());
        for (int i = 0; i < kFrameSamples; ++i)
            acc[i] += qint32(s[i] * gain);             // микс = сложение волн
    }
}

// ---------- часы звука наружу ----------

void AudioWorker::publishPlayheads() {
    const int sink = sinkQueuedMs();
    QMutexLocker lock(&m_phLock);
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it)
        m_playheads[it.key()] = { it->lastTs, it->lastTsAt,
                                  int(it->queue.size()) * kFrameMs + sink };
}

void AudioWorker::forgetPlayhead(quint32 id) {
    QMutexLocker lock(&m_phLock);
    m_playheads.remove(id);
}

qint64 AudioWorker::playheadMs(quint32 id) const {
    Playhead ph;
    {
        QMutexLocker lock(&m_phLock);
        const auto it = m_playheads.constFind(id);
        if (it == m_playheads.constEnd()) return 0;
        ph = *it;
    }
    if (ph.lastTs == 0) return 0;
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - ph.lastTsAt;
    if (age > 700) return 0;      // звук иссяк (мик выключили) — не держим видео
    // Всё, что лежит в очереди и в синке, ещё НЕ прозвучало — значит слышимый
    // сейчас звук соответствует более ранней метке отправителя.
    return ph.lastTs - ph.bufMs + age;
}
