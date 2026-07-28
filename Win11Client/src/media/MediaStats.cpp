#include "MediaStats.h"
#include "../net/SignalingClient.h"
#include <QTimer>

MediaStats::MediaStats(SignalingClient* conf, QObject* parent) : QObject(parent) {
    // Входящий трафик считаем прямо на границе сокета: так в него попадают все
    // полосы сразу, включая те, которых движки не разбирают.
    connect(conf, &SignalingClient::binaryFrame, this,
            [this](const QByteArray& d) { m_rxBytes += d.size(); });

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
void MediaStats::noteTxDrop(bool screen)    { ++band(screen).drops; }

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
    if (screen) { m_scrAudio = false; m_scrAudioSeen = false; }
    emit updated();
}

void MediaStats::noteRxFrame(quint32 sender) {
    ++m_rxFrames;
    m_rxSenders.insert(sender);
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
        && m_voice.kbps == 0 && m_syncHoldMs == 0 && !m_scrAudio;
}

void MediaStats::tick() {
    const bool wasQuiet = isQuiet();
    const qint64 el = qMax<qint64>(1, m_window.restart());
    // байты * 8 = биты; биты / миллисекунды = килобиты в секунду.
    const auto kbps  = [el](qint64 bytes) { return int(bytes * 8 / el); };
    const auto perSec = [el](int n) { return int(qint64(n) * 1000 / el); };

    const int rxKbps = kbps(m_rxBytes);
    const int rxFps = perSec(m_rxFrames);
    const int rxStreams = m_rxSenders.size();

    const auto close = [&](Band& b) {
        b.kbps = kbps(b.bytes);
        b.fps = perSec(b.attempts - b.drops);
        b.dropPercent = b.attempts > 0 ? int(qint64(b.drops) * 100 / b.attempts) : 0;
        b.bytes = 0; b.attempts = 0; b.drops = 0;
    };

    m_rxKbps = rxKbps; m_rxFps = rxFps; m_rxStreams = rxStreams;
    m_rxBytes = 0; m_rxFrames = 0; m_rxSenders.clear();

    close(m_cam);
    close(m_scr);
    close(m_voice);
    m_scrAudio = m_scrAudioSeen;
    m_scrAudioSeen = false;

    m_syncHoldMs = m_holdCount > 0 ? int(m_holdSum / m_holdCount) : 0;
    m_holdSum = 0; m_holdCount = 0;

    // Молчим, только если и было тихо, и осталось тихо: последний тик активной
    // конференции обязан дойти до раздела — иначе на нём навсегда останутся
    // цифры секундной давности.
    if (!wasQuiet || !isQuiet()) emit updated();
}
