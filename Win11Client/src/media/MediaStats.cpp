#include "MediaStats.h"
#include "../net/SignalingClient.h"
#include <QTimer>

MediaStats::MediaStats(SignalingClient* conf, QObject* parent)
    : QObject(parent), m_conf(conf) {
    // Входящий трафик считает сам транспорт (атомик), а мы забираем накопленное
    // раз в секунду. Сигналом на каждый кадр это делать больше нельзя: звук
    // теперь идёт мимо GUI-потока, и будить его полсотни раз в секунду ради
    // одного сложения — ровно та работа, от которой мы уходили.
    m_window.start();
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &MediaStats::tick);
    m_timer->start();
}

void MediaStats::noteTxVideo(bool screen, int bytes) { band(screen).bytes += bytes; }

void MediaStats::noteTxAudio(bool screen, int bytes) {
    if (screen) { m_scr.bytes += bytes; m_scrAudioSeen = true; }
    else        m_voice.bytes += bytes;
}

void MediaStats::noteTxAttempt(bool screen) { ++band(screen).attempts; }

void MediaStats::noteTxDrop(bool screen, DropReason why) {
    Band& b = band(screen);
    ++b.drops;
    if (why == DropBusy) ++b.dropsBusy;
}

// Случается редко (открытие кодировщика), поэтому сообщаем сразу, не дожидаясь
// тика: строка формата — то немногое, что видно ещё до первых килобит.
void MediaStats::noteEncoder(bool screen, const QString& codec, int w, int h) {
    band(screen).format = QStringLiteral("%1 · %2×%3").arg(codec).arg(w).arg(h);
    emit updated();
}

// Полоса выключена. Формат стираем сразу, не дожидаясь тика: «H.264 · 1280×720»
// под выключенной камерой читается как «камера всё ещё вещает».
void MediaStats::noteTxOff(bool screen) {
    Band& b = band(screen);
    b = Band();
    (screen ? m_scrEnc : m_camEnc) = Timing();
    if (screen) { m_scrAudio = false; m_scrAudioSeen = false; }
    emit updated();
}

void MediaStats::noteRxFrame(quint32 sender) {
    ++m_rxFrames;
    m_rxSenders.insert(sender);
}

void MediaStats::noteEncodeTime(bool screen, qint64 micros) {
    Timing& t = screen ? m_scrEnc : m_camEnc;
    t.sumUs += micros;
    t.peakUs = qMax(t.peakUs, micros);
    ++t.n;
}

void MediaStats::noteDecodeTime(bool screen, qint64 micros) {
    Timing& t = screen ? m_scrDec : m_camDec;
    t.sumUs += micros;
    t.peakUs = qMax(t.peakUs, micros);
    ++t.n;
}

// Как и у кодировщика (noteEncoder), сообщаем сразу: строка появляется раньше
// первых замеров времени и объясняет их — «HEVC» без пометки про видеокарту и
// есть ответ на вопрос, почему кадр стоит тридцать миллисекунд.
void MediaStats::noteDecoder(bool screen, const QString& codec, int w, int h) {
    (screen ? m_scrDecoder : m_camDecoder) =
        QStringLiteral("%1 · %2×%3").arg(codec).arg(w).arg(h);
    emit updated();
}

// Длина очереди — мгновенная (её нельзя усреднить: важно именно то, сколько
// стоит прямо сейчас), а выброшенные кадры копятся до конца окна.
void MediaStats::noteRxQueue(int queued, int droppedSinceLast) {
    m_rxQueue = queued;
    m_rxDropAccum += droppedSinceLast;
}

void MediaStats::noteRxOff(bool screen) {
    QString& s = screen ? m_scrDecoder : m_camDecoder;
    if (s.isEmpty()) return;
    s.clear();
    (screen ? m_scrDec : m_camDec) = Timing();
    emit updated();
}

void MediaStats::noteSyncHold(qint64 ms) {
    m_holdSum += ms;
    ++m_holdCount;
}

// Раз в секунду: счётчики -> скорости. Окно меряем часами, а не считаем ровно
// секундой: таймер может опоздать под нагрузкой, и тогда «килобиты в секунду»
// оказались бы завышены ровно в момент, когда на них смотрят внимательнее всего.
// Всё по нулям: ни приёма, ни отправки. Вне конференции это девять секунд из
// десяти, и пересчитывать ради этого привязки открытого раздела незачем.
bool MediaStats::isQuiet() const {
    return m_rxKbps == 0 && m_rxFps == 0 && m_rxStreams == 0
        && m_cam.kbps == 0 && m_cam.fps == 0 && m_cam.dropPercent == 0
        && m_scr.kbps == 0 && m_scr.fps == 0 && m_scr.dropPercent == 0
        && m_voice.kbps == 0 && m_syncHoldMs == 0 && !m_scrAudio
        && m_camDecoder.isEmpty() && m_scrDecoder.isEmpty()
        && m_rxQueue == 0 && m_rxDropped == 0;
}

void MediaStats::tick() {
    const bool wasQuiet = isQuiet();
    const qint64 el = qMax<qint64>(1, m_window.restart());
    // байты * 8 = биты; биты / миллисекунды = килобиты в секунду.
    const auto kbps  = [el](qint64 bytes) { return int(bytes * 8 / el); };
    const auto perSec = [el](int n) { return int(qint64(n) * 1000 / el); };

    const int rxKbps = kbps(m_conf->takeRxBytes());
    const int rxFps = perSec(m_rxFrames);
    const int rxStreams = m_rxSenders.size();

    const auto close = [&](Band& b) {
        b.kbps = kbps(b.bytes);
        b.fps = perSec(b.attempts - b.drops);
        const auto share = [&b](int n) {
            return b.attempts > 0 ? int(qint64(n) * 100 / b.attempts) : 0;
        };
        b.dropPercent = share(b.drops);
        b.dropBusyPercent = share(b.dropsBusy);
        b.dropCongestionPercent = share(b.drops - b.dropsBusy);
        b.bytes = 0; b.attempts = 0; b.drops = 0; b.dropsBusy = 0;
    };
    // Окно без единого кадра оставляет прошлые числа как есть, а не обнуляет:
    // на статичном экране кадров может не быть секунду-другую, и мигающий ноль
    // читался бы как «кодировщик перестал работать».
    const auto closeTiming = [](Timing& t) {
        if (t.n <= 0) return;
        t.avg = double(t.sumUs) / t.n / 1000.0;
        t.peak = double(t.peakUs) / 1000.0;
        t.sumUs = 0; t.peakUs = 0; t.n = 0;
    };

    m_rxKbps = rxKbps; m_rxFps = rxFps; m_rxStreams = rxStreams;
    m_rxFrames = 0; m_rxSenders.clear();

    close(m_cam);
    close(m_scr);
    close(m_voice);
    closeTiming(m_camEnc);
    closeTiming(m_scrEnc);
    closeTiming(m_camDec);
    closeTiming(m_scrDec);
    m_scrAudio = m_scrAudioSeen;
    m_scrAudioSeen = false;

    m_syncHoldMs = m_holdCount > 0 ? int(m_holdSum / m_holdCount) : 0;
    m_holdSum = 0; m_holdCount = 0;
    m_rxDropped = m_rxDropAccum;
    m_rxDropAccum = 0;

    // Молчим, только если и было тихо, и осталось тихо: последний тик активной
    // конференции обязан дойти до раздела — иначе на нём навсегда останутся
    // цифры секундной давности.
    if (!wasQuiet || !isQuiet()) emit updated();
}
