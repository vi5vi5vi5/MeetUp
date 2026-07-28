#pragma once
#include <QObject>
#include <QAudioDevice>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMutex>

class QAudioSink;
class QAudioSource;
class QIODevice;
class QTimer;
class ScreenAudioCapture;
struct OpusEncoder;   // C-структуры из opus.h — вперёд объявляются как struct
struct OpusDecoder;

// Звук целиком: захват микрофона, кодек, приём и декод, джиттер-буфер, микс и
// вывод в динамики. Живёт на ОТДЕЛЬНОМ потоке; GUI-поток сюда не заходит.
//
// Зачем поток. Насос вывода тикает каждые 10 мс и держит в звуковой карте всего
// ~60 мс запаса. На GUI-потоке этот тик не переживает ни модальный цикл размера
// окна у Windows (таймеры Qt доставляются сообщениями, а они в этом цикле
// голодают), ни блокирующую синхронизацию Qt Quick на ресайзе. Пауза длиннее
// запаса — это опустевший буфер устройства, то есть щелчок в динамиках. Ровно
// по этой же причине кодирование видео уже вынесено в VideoSendWorker.
//
// Наружу — только сигналы и слоты: кто и когда включает захват, решает
// AudioEngine на GUI-потоке, здесь лежит сам звук. Единственное исключение —
// playheadMs(): его синхронно спрашивает VideoEngine, поэтому часы публикуются
// снимком под мьютексом.
class AudioWorker : public QObject {
    Q_OBJECT
public:
    explicit AudioWorker(QObject* parent = nullptr);
    ~AudioWorker() override;

    // Где сейчас «играющий» звук участника в шкале часов ОТПРАВИТЕЛЯ (мс):
    // метка последнего чанка минус то, что ещё лежит в очереди и в синке, плюс
    // прошедшее с тех пор время. 0 — звука нет или он устарел; тогда видео не
    // придерживают. По этому значению VideoEngine синхронизирует губы.
    // Зовётся с GUI-потока — читает снимок под мьютексом.
    qint64 playheadMs(quint32 id) const;

public slots:
    void init();         // поток стартовал: COM, таймеры, захват звука машины
    void shutdown();     // поток останавливается: закрыть устройства

    // Устройства выбирает GUI (QMediaDevices — только оттуда) и присылает
    // готовыми значениями. Изменилось — перезапускаем ту сторону, которой оно
    // касается; декодеры участников при смене динамиков живут дальше.
    void setDevices(const QAudioDevice& input, const QAudioDevice& output);
    void setCapture(bool on);        // микрофон: снимать или нет
    void setPlayback(bool on);       // вывод: играть или нет
    void setScreenAudio(bool on);    // звук демонстрации: снимать или нет
    void setBitrate(int bps);        // Opus голоса — на лету, без пересоздания
    void setGains(qreal volume, qreal sensitivity, qreal screenVolume);
    void setOutputMuted(bool muted);

    void onFrame(const QByteArray& frame);   // входящий кадр v2 (полоса звука)
    void resetPeers();                       // join_ok/выход: буферы — мусор
    void dropPeer(qint64 id);                // участник ушёл

signals:
    void packetReady(const QByteArray& frame);   // готовый кадр v2 -> транспорт
    void txAudio(bool screen, int bytes);        // -> MediaStats
    void micLevel(qreal level);                  // -> индикатор в настройках
    void selfSpeaking();                         // мы говорим (подсветка плитки)
    void speaking(qint64 id);                    // говорит участник
    void screenLive(bool on);                    // звук демонстрации идёт
    void screenFailed(const QString& text);      // захват звука машины не поднялся

private:
    // Приёмная сторона одного участника: декодер + джиттер-буфер.
    struct Peer {
        OpusDecoder* dec = nullptr;
        QList<QByteArray> queue;    // декодированные кадры по 1920 байт
        bool playing = false;       // предбуфер пройден
        qint64 lastTs = 0;          // метка последнего принятого чанка
        qint64 lastTsAt = 0;        // когда он к нам пришёл (локальные мс)
        qint64 lastSpokeAt = 0;     // когда в последний раз зажигали «говорит»
        // Адаптивный запас. Держать всегда «на всякий случай» побольше — значит
        // всегда разговаривать с задержкой; поэтому запас растёт только когда
        // буфер реально опустел, и медленно сползает обратно, когда тихо.
        int prebuf = 3;             // кадров копим перед стартом (60 мс)
        int plcRun = 0;             // подряд синтезированных кадров (PLC)
        qint64 calmSince = 0;       // играем без провалов с этого момента
    };

    // Часы звука наружу: то, что VideoEngine читает с GUI-потока.
    struct Playhead {
        qint64 lastTs = 0;
        qint64 lastTsAt = 0;
        int bufMs = 0;              // ещё не прозвучало: очередь + синк
    };

    void startCapture();
    void stopCapture();
    void startPlayback();
    void stopPlayback();
    void startScreenAudio();
    void stopScreenAudio();
    void onCaptured();                          // микрофон принёс сэмплы
    void onScreenPcm(const QByteArray& pcm, qint64 wallMs);
    void pump();                                // насос: подкормить синк миксом
    QByteArray mixOneFrame();                   // один кадр микса всех участников
    void mixInto(qint32* acc, QHash<quint32, Peer>& peers, qreal gain, qint64 now);
    int  sinkQueuedMs() const;                  // сколько звука лежит в QAudioSink
    void publishPlayheads();                    // обновить снимок часов
    void forgetPlayhead(quint32 id);
    void setScreenLive(bool on);
    void destroyPeers(QHash<quint32, Peer>& peers);

    QAudioDevice m_inDev, m_outDev;
    bool m_comReady = false;            // COM на этом потоке подняли мы
    bool m_wantCapture = false, m_wantPlayback = false, m_wantScreenAudio = false;
    bool m_outputMuted = false;
    int  m_bitrate = 32000;
    qreal m_volGain = 1.0, m_sensGain = 1.0, m_scrVolGain = 1.0;

    QAudioSource* m_source = nullptr;   // захват (жив только пока говорим)
    QIODevice* m_mic = nullptr;         // поток сэмплов (принадлежит m_source)
    QByteArray m_pcm;                   // накопитель до полного кадра
    OpusEncoder* m_enc = nullptr;       // кодер (жив вместе с захватом)
    qint64 m_audioClockMs = 0;          // монотонные часы меток отправки
    qint64 m_micLevelAt = 0;            // прореживание индикатора уровня
    qint64 m_selfSpokeAt = 0;           // прореживание «говорит» для себя

    ScreenAudioCapture* m_scrCapture = nullptr;   // захват звука машины
    OpusEncoder* m_scrEnc = nullptr;              // свой кодер: музыка, не голос
    qint64 m_scrClockMs = 0;                      // монотонные метки этой полосы
    bool m_scrLive = false;                       // звук демонстрации приходит
    qint64 m_scrRxAt = 0;                         // когда пришёл последний кадр
    QTimer* m_scrLiveTimer = nullptr;             // тикает, только пока идёт звук

    QHash<quint32, Peer> m_peers;       // ключ — sender из заголовка кадра
    // Звук демонстрации приходит отдельным потоком и от того же участника, что
    // и его голос, — поэтому вторая карта, а не общая: у них свои декодеры,
    // свои буферы, и по ним НЕ синхронизируются губы (там нет губ).
    QHash<quint32, Peer> m_scrPeers;

    QAudioSink* m_sink = nullptr;
    QIODevice* m_out = nullptr;         // куда пишем микс (принадлежит m_sink)
    QTimer* m_pumpTimer = nullptr;

    mutable QMutex m_phLock;
    QHash<quint32, Playhead> m_playheads;
};
