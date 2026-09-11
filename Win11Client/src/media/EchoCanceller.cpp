#include "EchoCanceller.h"
#include <QDebug>
#include <QtMath>
#include <cmath>
#include <cstring>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

static const int kRate = 48000;
static const int kFrame = 960;                       // 20 мс — кадр конференции
static const int kFrameBytes = kFrame * 2;
static const int kTailMs = 400;                      // хвост фильтра
// Запас очереди опорного потока (после чтения кадра), с которого начинаем:
// два кадра под всплески планировщика.
static const int kTargetFrames = 2;                  // 40 мс
// Порог дискретной починки смещения (см. .h): полтора кадра. Ниже — оставляем
// петле, выше — двигаем курсор разом. 30 мс лишней или недостающей задержки эха
// фильтру безразличны, они внутри хвоста.
static const int kRealign = kFrame * 3 / 2;          // 1440 сэмплов = 30 мс
// Петля дрейфа: ошибка — в сэмплах запаса, поправка — в долях частоты. Запас
// очереди — это ИНТЕГРАЛ расхождения частот, поэтому петля второго порядка;
// пара kp/ki задаёт её как звено с omega = 0.15 рад/с и затуханием 1,2, то есть
// сходимость за 20–30 с без перерегулирования. Медленнее нельзя: пока дрейф не
// найден, фильтр живёт с уползающим эхом и даёт единицы децибел вместо двадцати
// (проверено синтетикой: при omega = 0.05 петля не успевала и за четыре минуты).
static const double kDriftKp = 7.5e-6;
static const double kDriftKi = 4.7e-7;
static const double kDriftMax = 1e-3;                // 1000 ppm — больше не дрейф, а поломка
// Первые тики петля только присматривается: синк в это время ещё наполняется до
// своего рабочего запаса, «прозвучало» отстаёт от «записано» на растущую
// величину, и это выглядит как чудовищный дрейф. Одна секунда — вдвое больше,
// чем живёт запас синка (60 мс), и на порядок меньше сходимости петли.
static const int kWarmupTicks = 2;
// Мёртвая зона крошечная — треть миллисекунды: на общем клоке запас стоит
// намертво и петля честно показывает ноль, а порогу побольше ловить нечего —
// он бы только тормозил сходимость (перерегулирование по запасу — десятки
// сэмплов, а не сотни).
static const double kDriftDeadband = 16;
// Ниже этого RMS опорный кадр считаем тишиной: вычитать из микрофона нечего,
// и статистику подавления такой кадр не должен ни портить, ни украшать.
static const double kFarSilenceRms = 32768.0 * 0.003;   // ≈ −50 dBFS

// Кубическая интерполяция (Катмулл-Ром) по четырём сэмплам вокруг дробной
// позиции. Вызывающий обязан гарантировать, что i-1 и i+2 существуют.
static inline double interpCubic(const qint16* s, size_t i, double t) {
    const double a0 = s[i - 1], a1 = s[i], a2 = s[i + 1], a3 = s[i + 2];
    return 0.5 * ((2 * a1)
                  + (-a0 + a2) * t
                  + (2 * a0 - 5 * a1 + 4 * a2 - a3) * t * t
                  + (-a0 + 3 * a1 - 3 * a2 + a3) * t * t * t);
}

EchoCanceller::EchoCanceller() {
    m_echo = speex_echo_state_init(kFrame, kRate * kTailMs / 1000);
    if (!m_echo) {
        qWarning() << "EchoCanceller: speex_echo_state_init не дал состояния — эхоподавление выключено";
        return;
    }
    int rate = kRate;
    speex_echo_ctl(m_echo, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);

    m_pre = speex_preprocess_state_init(kFrame, kRate);
    if (m_pre) {
        // У препроцессора остаётся ровно одна работа — остаточное эхо. Шум не
        // его дело (следом стоит RNNoise), усиление тоже (AutoGain). Denoise
        // при этом формально включён: выключенный он обнуляет и подавление
        // эха — так устроен preprocess.c, — а порог шума в 0 дБ делает его
        // пустым. VAD не трогаем даже ради «выключить»: он и так выключен, а
        // сам вызов печатает в лог предупреждение Speex о его состоянии.
        int off = 0, noiseDb = 0;
        speex_preprocess_ctl(m_pre, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noiseDb);
        speex_preprocess_ctl(m_pre, SPEEX_PREPROCESS_SET_AGC, &off);
        speex_preprocess_ctl(m_pre, SPEEX_PREPROCESS_SET_DEREVERB, &off);
        speex_preprocess_ctl(m_pre, SPEEX_PREPROCESS_SET_ECHO_STATE, m_echo);
        int sup = -40, supActive = -15;   // в паузах давим сильнее, поверх речи мягче
        speex_preprocess_ctl(m_pre, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, &sup);
        speex_preprocess_ctl(m_pre, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE, &supActive);
    } else {
        qWarning() << "EchoCanceller: препроцессор не поднялся — остаточное эхо давить нечем";
    }

    m_out.resize(kFrameBytes);
    m_play.assign(kFrame, 0);
    m_ref.reserve(kRate);
    m_impulse.resize(kRate * kTailMs / 1000);
}

EchoCanceller::~EchoCanceller() {
    if (m_pre) speex_preprocess_state_destroy(m_pre);
    if (m_echo) speex_echo_state_destroy(m_echo);
}

// Сыгранный кадр — в очередь. Заодно двигаем часы вывода: сколько записано и
// сколько из этого уже прозвучало (см. .h — это и есть измеритель дрейфа).
// Выравнивание здесь не трогаем: его осмысленно смотреть в одном месте — после
// того, как кадр микрофона своё прочитал.
void EchoCanceller::playback(const QByteArray& frame, int sinkQueuedSamples) {
    if (!m_echo || frame.size() != kFrameBytes) return;
    const qint16* in = reinterpret_cast<const qint16*>(frame.constData());
    m_ref.insert(m_ref.end(), in, in + kFrame);
    m_written += kFrame;
    // Остаток синка не бывает больше записанного; отрицательный остаток —
    // «не знаю» от вызывающего, и тогда дрейф не ищем вовсе.
    m_played = sinkQueuedSamples < 0 ? -1
             : qMax<qint64>(0, m_written - qint64(sinkQueuedSamples));
}

void EchoCanceller::dropPlayback() {
    if (m_echo) speex_echo_state_reset(m_echo);
    m_delayMs = -1;
    m_erleDb = 0;
    // Часы синка начнутся заново — выравнивание задастся на первом же кадре.
    m_written = 0;
    m_played = -1;
    m_playedPrev = -1;
    m_consumed = 0;
    m_aligned = false;
}

void EchoCanceller::capture(qint16* samples, int count) {
    if (!m_echo || count != kFrame) return;

    // Первый кадр микрофона задаёт выравнивание раз и навсегда. Насос к этому
    // моменту положил в очередь два-три кадра; если меньше — дописываем тишину
    // В НАЧАЛО, чтобы после чтения остался ровно целевой запас. Тишина спереди
    // делает опорный сигнал чуть «старше» — для фильтра это лишь чуть большая
    // задержка эха, а запас под всплески планировщика обязателен: без него
    // первый же опоздавший кадр насоса сдвинул бы выравнивание.
    if (!m_aligned) {
        m_aligned = true;
        // +2 сэмпла — хвост под кубическую интерполяцию, +1 спереди — история.
        const size_t want = size_t(kTargetFrames * kFrame) + kFrame + 3;
        if (m_ref.size() < want) m_ref.insert(m_ref.begin(), want - m_ref.size(), qint16(0));
        m_pos = 1;                                   // один сэмпл истории позади
        m_consumed = 0;
        m_target = m_played >= 0 ? double(m_played) : 0.0;
        m_diffSum = 0;
        m_diffCount = 0;
        m_diffEma = m_target;
        m_warmup = kWarmupTicks;
    }

    // Чтение опорного кадра по дробной позиции: шаг чуть больше или меньше
    // единицы и есть поправка на дрейф.
    bool starved = false;
    for (int i = 0; i < kFrame; ++i) {
        const size_t idx = size_t(m_pos);
        if (idx + 2 >= m_ref.size()) {               // насос не поспел
            std::memset(m_play.data() + i, 0, size_t(kFrame - i) * sizeof(qint16));
            starved = true;
            break;
        }
        const double v = interpCubic(m_ref.data(), idx, m_pos - double(idx));
        m_play[size_t(i)] = qint16(qBound(-32768.0, v, 32767.0));
        m_pos += m_step;
    }

    m_consumed += double(kFrame) * m_step;

    // Разовое смещение — чиним разом (см. .h): очередь в буфере длиннее цели —
    // двигаем курсор вперёд, короче — вставляем тишину перед ним. Фильтр
    // переучится за секунду, зато опорный сигнал не приходится тянуть
    // интерполятором, а значит, мы не создаём собственного дрейфа. Смещение —
    // это не дрейф, поэтому после починки (и после голодания, где нас уже
    // сдвинула добитая тишина) просто принимаем новое выравнивание за исходное:
    // петля обязана держать разницу постоянной, а не возвращать её к прежней.
    const double level = double(m_ref.size()) - m_pos;
    const double off = level - double(kTargetFrames * kFrame);
    bool rebase = starved;
    if (starved) ++m_resyncs;          // голодание — тоже сбой выравнивания
    if (!starved && off > kRealign) {
        m_pos += off;
        m_consumed += off;
        rebase = true;
        ++m_resyncs;
    } else if (!starved && off < -kRealign) {
        const size_t at = size_t(m_pos);
        m_ref.insert(m_ref.begin() + long(at), size_t(-off), qint16(0));
        m_consumed += off;                 // off < 0: опорный поток стал «старше»
        rebase = true;
        ++m_resyncs;
    }

    // Расхождение часов вывода и чтения — то, что ведёт петля дрейфа. Копим
    // СРЕДНЕЕ за секунду, а не минимум: «прозвучало» обновляется только когда
    // насос пишет кадр, а пишет он не на каждый наш кадр — в отдельных кадрах
    // число оказывается на кадр устаревшим. Минимум цеплялся ровно за такие
    // выбросы и дёргал петлю на сотни ppm (видно в синтетике как провал
    // подавления раз в пару минут); среднее их размывает, а на измерение
    // дрейфа постоянный сдвиг не влияет вовсе.
    if (m_played >= 0) {
        const double diff = double(m_played) - m_consumed;
        if (rebase) {
            m_target = diff;
            m_diffEma = diff;
            m_diffSum = 0;
            m_diffCount = 0;
        } else {
            m_diffSum += diff;
            ++m_diffCount;
        }
    }
    // Хвост очереди подрезаем не на каждом кадре, а пачками: сдвиг вектора
    // стоит дороже, чем лишние 48 000 сэмплов памяти. Один сэмпл истории
    // оставляем — он нужен интерполятору.
    if (m_pos > double(kRate)) {
        const size_t drop = size_t(m_pos) - 1;
        m_ref.erase(m_ref.begin(), m_ref.begin() + long(drop));
        m_pos -= double(drop);
    }
    const qint16* play = m_play.data();

    // Энергии до и после — ради диагностики, одним проходом.
    double eFar = 0, eIn = 0, eOut = 0;
    for (int i = 0; i < kFrame; ++i) {
        eFar += double(play[i]) * play[i];
        eIn  += double(samples[i]) * samples[i];
    }

    // Не на месте: mdf копирует вход прежде, чем писать выход, но полагаться
    // на это — экономить один memcpy ценой обращения к чужому исходнику.
    qint16* out = reinterpret_cast<qint16*>(m_out.data());
    speex_echo_cancellation(m_echo, samples, play, out);
    if (m_pre) speex_preprocess_run(m_pre, out);
    for (int i = 0; i < kFrame; ++i) eOut += double(out[i]) * out[i];
    std::memcpy(samples, out, size_t(kFrameBytes));

    // ---- диагностика ----
    m_farActive = eFar / kFrame > kFarSilenceRms * kFarSilenceRms;
    if (m_farActive && eIn > 0) {
        // Подавление по кадру, дБ. Зажато в 0..40: выше — уже не измерение,
        // а деление на цифровую тишину.
        const double db = qBound(0.0, 10.0 * std::log10(eIn / qMax(eOut, 1.0)), 40.0);
        m_erleDb = m_erleDb == 0 ? db : m_erleDb * 0.9 + db * 0.1;
    }
    if (++m_frames % 50 == 0) {                 // раз в секунду
        updateDrift();
        updateDelay();
    }
}

// Петля дрейфа: запас очереди ниже цели — вывод отстаёт от захвата, опорный
// поток надо растянуть (шаг < 1); выше — сжать. Интегральная часть и есть
// найденный дрейф; пропорциональная гасит колебания. Мёртвая зона держит петлю
// на нуле, пока расхождение меньше миллисекунды: на общем клоке оно и не
// вырастет, а лишний уход опорного сигнала по времени дороже точности.
void EchoCanceller::updateDrift() {
    if (!m_aligned || m_played < 0) return;
    // Минимум за секунду, а не мгновенное значение: и запись в синк, и чтение
    // идут кадрами, поэтому разница пилит на кадр туда-сюда от одного лишь
    // планировщика. Минимум за секунду от этой пилы не зависит.
    // Пока синк наполняется, ошибку не считаем вовсе — принимаем то, что
    // намерили, за новую точку отсчёта (см. kWarmupTicks).
    if (m_diffCount == 0) return;               // насос молчал всю секунду
    const double mean = m_diffSum / m_diffCount;
    m_diffSum = 0;
    m_diffCount = 0;

    // Проверка самой меры. «Прозвучало» обязано расти примерно на частоту
    // дискретизации в секунду; не растёт — значит, вывод стоит (тогда и эха
    // нет) или QAudioSink на этой машине отдаёт остаток буфера не так, как мы
    // думаем. Второе — не повод тянуть опорный сигнал наугад: пропускаем тик,
    // и поправка остаётся прежней. Если так будет всегда, эхоподавитель просто
    // работает без компенсации дрейфа, как на общем клоке.
    const qint64 prev = m_playedPrev;
    m_playedPrev = m_played;
    if (prev < 0) return;
    const double rate = double(m_played - prev);
    if (rate < kRate * 0.9 || rate > kRate * 1.1) return;
    if (m_warmup > 0) {
        --m_warmup;
        m_target = mean;
        m_diffEma = mean;
        return;
    }
    m_diffEma = m_diffEma * 0.7 + mean * 0.3;
    double err = m_diffEma - m_target;
    if (qAbs(err) < kDriftDeadband) err = 0;
    err = qBound(double(-kRealign), err, double(kRealign));
    m_integral = qBound(-kDriftMax, m_integral + kDriftKi * err, kDriftMax);
    m_step = 1.0 + qBound(-kDriftMax, kDriftKp * err + m_integral, kDriftMax);
}

// Импульсный отклик пути «динамик → микрофон», как его видит фильтр: пик — это
// и есть задержка эха. Плоский отклик (ещё не сошёлся, динамики молчали) —
// ответа нет. Двадцать обратных БПФ раз в секунду — цена, которой не видно.
void EchoCanceller::updateDelay() {
    if (speex_echo_ctl(m_echo, SPEEX_ECHO_GET_IMPULSE_RESPONSE, m_impulse.data()) != 0) return;
    double sum = 0;
    qint64 peak = 0;
    int at = -1;
    for (int i = 0; i < m_impulse.size(); ++i) {
        const qint64 a = qAbs(qint64(m_impulse[i]));
        sum += double(a);
        if (a > peak) { peak = a; at = i; }
    }
    // Пик обязан заметно выделяться над средним: у несошедшегося фильтра
    // «пик» — просто самый большой из шумов.
    const double mean = sum / qMax(1, int(m_impulse.size()));
    m_delayMs = (at >= 0 && double(peak) > mean * 8) ? int(qint64(at) * 1000 / kRate) : -1;
}
