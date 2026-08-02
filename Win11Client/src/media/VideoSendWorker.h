#pragma once
#include <QObject>
#include <QByteArray>
#include <QRect>
#include <QVideoFrame>
#include <atomic>

class VideoEncoder;
class E2eCipher;
struct SwsContext;
struct AVFrame;

// Кодирование видео отправки живёт на ОТДЕЛЬНОМ потоке. sws_scale + H.264/VP8 —
// это единицы-десятки миллисекунд на кадр; на GUI-потоке они запинали бы
// отрисовку — при перетаскивании окна (модальный цикл перемещения у Windows)
// кадр энкодера блокировал бы обработку сообщений move, и окно дёргалось бы.
// Кадр камеры прилетает сюда queued-сигналом, наружу уходят готовые пакеты v2.
class VideoSendWorker : public QObject {
    Q_OBJECT
public:
    // msgType — тип кадра в заголовке v2: VIDEO_CODED (камера) или
    // SCREEN_CODED (демонстрация экрана). Всё остальное у полос одинаково,
    // поэтому воркеров просто два — со своими энкодерами и своей каденцией.
    VideoSendWorker(quint8 msgType, E2eCipher* cipher, QObject* parent = nullptr);
    ~VideoSendWorker() override;

    // ---- читается с ЛЮБОГО потока (движок спрашивает с GUI) ----
    // Занят ли воркер незакрытым кадром. Раньше о своей свободе он сообщал
    // сигналом frameDone на GUI-поток — и пока тот стоял на синхронизации с
    // отрисовкой QML, воркер простаивал уже свободным. Частота отправки
    // упиралась не в скорость кодирования, а в занятость интерфейса; на
    // демонстрации 4К это стоило половины кадров. Атомик снимает хоп целиком.
    bool busy() const { return m_inFlight.load(std::memory_order_acquire) > 0; }
    // Кадр отдан воркеру: считаем его до того, как уйдёт queued-вызов, иначе
    // между emit и началом encode() движок успел бы отправить второй.
    void noteQueued() { m_inFlight.fetch_add(1, std::memory_order_release); }

public slots:
    // Один кадр камеры -> пакеты. maxW/maxH/fps/bitrate — пресет качества;
    // forceKey — опорный кадр по требованию (KEYFRAME_REQ / новичок / реконнект),
    // каденс воркер ведёт сам. tsMs — Date.now() отправителя (уходит в
    // заголовок кадра, §5.3).
    void encode(const QVideoFrame& frame, int maxW, int maxH, int fps,
                int bitrate, bool forceKey, qint64 tsMs);
    // Захват остановлен / сменились параметры: энкодер в мусор, следующий
    // запуск начнётся с опорного кадра.
    void reset();
    // Ступень на лестнице кодеков своей полосы (0 = лучшее, см.
    // VideoEncoder::open). Двигает её только VideoEngine и только в ответ на
    // жалобу получателя. Смена требует переоткрытия энкодера — он держит
    // состояние потока кадров, — поэтому здесь же reset(); приёмники увидят
    // новый кодек и пересоздадут декодеры сами.
    void setCodecStep(int step);

signals:
    // Готовый пакет v2 (уже упакован Proto::pack). Уходит ПРЯМО на поток
    // сокета (SignalingLink::sendBinary), минуя GUI: тому в медиапути делать
    // нечего, а лишний хоп добавлял к каждому кадру задержку планировщика и
    // копию буфера в очереди событий интерфейса.
    void packetReady(const QByteArray& frame);
    // …а вот числа для «Диагностики» — на GUI-поток, где живёт MediaStats.
    // Отдельным сигналом, чтобы полезная нагрузка туда не ездила вовсе.
    void packetSent(int bytes);
    // Во сколько микросекунд обошёлся кадр целиком: приведение пикселей плюс
    // кодирование. Уходит на КАЖДЫЙ кадр, а не раз в секунду, потому что пик
    // важнее среднего, а усреднять умеет MediaStats.
    void frameEncoded(qint64 micros);
    // Кодировщик открыт: чем и в каком размере вещаем на самом деле. Настройки
    // этого не знают — там потолок разрешения, а кадр может выйти меньше, если
    // камера столько не даёт; кодека там нет вовсе. hardware — видеокарта или
    // процессор: выбора и тут нет, а знать человеку полезно — от этого зависит,
    // чего вообще ждать от машины. Единственный путь наружу: «Диагностика».
    void encoderOpened(int protoCodec, int width, int height, bool hardware);

private:
    void encodeFrame(const QVideoFrame& frame, int maxW, int maxH, int fps,
                     int bitrate, bool forceKey, qint64 tsMs);
    // Пересоздать масштабатор, если поменялись размеры или форматы.
    bool ensureSws(int sw, int sh, int srcFmt, int tw, int th, int dstFmt);
    // Сколько кадров в секунду мы РЕАЛЬНО отдаём — по интервалам между
    // вызовами encodeFrame. Кодировщику важно знать это, а не пресетное
    // пожелание: битрейт он делит на объявленную частоту.
    int achievedFps(int requestedFps) const;
    void noteInterval(qint64 tsMs, int requestedFps);

    quint8 m_msgType;
    E2eCipher* m_cipher;           // не владеем: общий на все потоки медиа
    int m_keyEveryMs;              // каденс опорных кадров, мс
    qint64 m_lastKeyAtMs = 0;      // когда отдали последний (0 — ещё ни разу)
    VideoEncoder* m_enc = nullptr;
    SwsContext* m_sws = nullptr;
    // Параметры, под которые собран m_sws. Раньше их сторожил
    // sws_getCachedContext, но через него нельзя задать число потоков —
    // приходится собирать контекст руками и сравнивать самим.
    int m_swsSrcW = 0, m_swsSrcH = 0, m_swsSrcFmt = -1;
    int m_swsDstW = 0, m_swsDstH = 0, m_swsDstFmt = -1;
    bool m_keyNext = true;
    qint64 m_sendStartMs = 0;      // база внутренней PTS-шкалы кодера
    int m_codecStep = 0;           // ступень лестницы кодеков, 0 = лучшее
    std::atomic<int> m_inFlight{ 0 };   // см. busy()
    // Измерение реальной частоты (см. achievedFps).
    qint64 m_lastEncodeMs = 0;
    double m_ivlEmaMs = 0;         // среднее время между кадрами, мс
    int m_ratedFrames = 0;         // сколько интервалов легло в среднее
    int m_openedFps = 0;           // с какой частотой открыт текущий кодировщик
    int m_openedBitrate = 0;       // …и с каким битрейтом
    qint64 m_openedAtMs = 0;
};
