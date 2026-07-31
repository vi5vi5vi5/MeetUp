#include "Denoiser.h"
#include <QDebug>
#include <rnnoise.h>

// RNNoise работает во float, но в ШКАЛЕ Int16 (±32768), а не в привычных
// ±1.0. Это главная ловушка его API: поделив на 32768 «как обычно», получаешь
// сигнал на четыре порядка тише всего, на чём сеть обучалась, и она честно
// принимает его за тишину — на выходе молчание вместо голоса.

Denoiser::Denoiser() {
    m_st = rnnoise_create(nullptr);     // nullptr — встроенная модель
    if (!m_st) {
        qWarning() << "Denoiser: rnnoise_create не дал состояния — шумоподавление выключено";
        return;
    }
    const int fs = frameSize();
    m_in.resize(fs);
    m_out.resize(fs);
}

Denoiser::~Denoiser() {
    if (m_st) rnnoise_destroy(m_st);
}

int Denoiser::frameSize() {
    return rnnoise_get_frame_size();
}

float Denoiser::process(qint16* samples, int count) {
    if (!m_st || !samples) return 0.f;

    const int fs = frameSize();
    float vad = 0.f;

    for (int off = 0; off + fs <= count; off += fs) {
        for (int i = 0; i < fs; ++i)
            m_in[i] = float(samples[off + i]);

        const float p = rnnoise_process_frame(m_st, m_out.data(), m_in.constData());
        if (p > vad) vad = p;

        // Обратно с ограничением: подавление меняет форму волны, и на краях
        // выход выходит за Int16 — без клампа это переполнение и щелчок.
        for (int i = 0; i < fs; ++i) {
            const float v = m_out[i];
            samples[off + i] = v <= -32768.f ? qint16(-32768)
                             : v >=  32767.f ? qint16(32767)
                                             : qint16(v);
        }
    }
    return vad;
}
