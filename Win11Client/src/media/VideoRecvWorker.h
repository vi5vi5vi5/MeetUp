#pragma once
#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QVideoFrame>

class VideoDecoder;
class E2eCipher;
class QTimer;
struct SwsContext;
struct AVFrame;

// Декодирование ВХОДЯЩЕГО видео на отдельном потоке: декодер и нормализатор
// пикселей на каждого отправителя, ожидание опорного кадра, готовый QVideoFrame
// наружу. Зеркало VideoSendWorker — та же схема, только в обратную сторону.
//
// Зачем поток. H.264-декод чужой демонстрации 1080p плюс sws_scale — это
// единицы-десятки миллисекунд НА КАЖДЫЙ кадр. На GUI-потоке они отнимали время
// у отрисовки: интерфейс подрагивал ровно тогда, когда на него смотрят —
// во время чужой демонстрации. Рисовать кадр всё равно обязан GUI-поток
// (QVideoSink принадлежит плитке), но наружу отсюда уходит уже готовый
// QVideoFrame: он неявно расшарен, и его передача ничего не копирует.
//
// Потоков два, по одному на полосу, по той же причине, что и у отправки: кадр
// лица не должен ждать, пока декодируется кадр экрана.
class VideoRecvWorker : public QObject {
    Q_OBJECT
public:
    // codedType/jpegType — типы кадров ЭТОЙ полосы в заголовке v2
    // (VIDEO_CODED/VIDEO_JPEG у камеры, SCREEN_* у демонстрации). Всё
    // остальное воркер пропускает мимо: транспорт вещает обе полосы обоим.
    VideoRecvWorker(quint8 codedType, quint8 jpegType, E2eCipher* cipher,
                    QObject* parent = nullptr);
    ~VideoRecvWorker() override;

public slots:
    void init();                       // поток стартовал: сторож декодеров
    void shutdown();                   // поток встаёт: декодеры в мусор
    void onFrame(const QByteArray& frame);        // сырой кадр v2 из транспорта
    // Есть ли куда рисовать кадры этого отправителя (у плитки появился/пропал
    // QVideoSink). Пока некуда — не тратим процессор на декод в никуда, но
    // помним, что поток прерван: следующей плитке нужен опорный кадр.
    void setWanted(quint32 sender, bool wanted);
    void dropPeer(quint32 sender);     // участник ушёл
    void reset();                      // join_ok / выход: всё состояние — мусор

signals:
    // Готовый кадр -> GUI-поток, он отдаст его плитке.
    void frameReady(quint32 sender, const QVideoFrame& frame, qint64 tsMs);
    // Нужен опорный кадр (дельты без него — мусор). Просьбу шлёт VideoEngine —
    // у него ограничитель частоты. Полосу называем: опорный кадр экрана стоит
    // до четверти мегабайта, и просить его заодно с кадром камеры — значит
    // ронять канал ради того, о чём никто не просил.
    void keyframeNeeded(quint8 band);
    // Поток отправителя прерван / поехал дальше. GUI держит зеркало этого
    // флага — по нему плитка при рождении решает, показывать ли аватарку
    // вместо застывшего кадра.
    void awaitKeyChanged(quint32 sender, bool awaiting);
    // Кадр пришёл с кодеком, которого мы не знаем — или знаем, но взять не
    // можем. Наружу — чтобы движок сказал об этом отправителю
    // (Proto::CODEC_UNSUPPORTED): молча ронять такие кадры значит оставить и
    // себя без картинки, и ведущего без объяснения. Прорежено по времени:
    // поток кадров идёт постоянно, а сообщение нужно одно.
    void codecUnsupported(quint8 codec);
    // Во сколько микросекунд обошёлся разбор кадра (вместе с приведением
    // пикселей) — зеркало VideoSendWorker::frameEncoded.
    void frameDecoded(qint64 micros);
    // Декодер отдал первый кадр: чем и в каком размере разбираем и взялась ли
    // за это видеокарта. Раньше первого кадра сказать нечего — драйвер вправе
    // отказаться уже на разборе заголовков.
    void decoderOpened(int protoCodec, int width, int height, bool hardware);
    // Кадры участника не открываются нашим ключом (или открылись снова).
    // Только на переходах — см. AudioWorker::peerLocked.
    void peerLocked(qint64 id, bool locked);

private:
    // Приёмная сторона одного отправителя.
    struct Peer {
        VideoDecoder* dec = nullptr;   // создаётся под первый кадр
        SwsContext* sws = nullptr;     // нормализатор пикселей (кэшируется)
        bool awaitKey = true;          // дельты без опорного кадра — мусор
        bool wanted = false;           // плитка есть и ждёт кадры
        qint64 lastAt = 0;             // когда декодировали последний кадр
        int cryptoFails = 0;           // подряд не открывшихся кадров
        bool locked = false;           // …и вывод: у него другой ключ
        bool announced = false;        // о декодере уже сказали наружу
        // Успеваем ли мы за потоком. Смысл в паре чисел: сколько стоит кадр и
        // сколько времени между кадрами. Если разбор съедает почти весь
        // интервал, запаса нет — любой всплеск начнёт копить отставание,
        // и его уже не отдать: очередь кадров растёт, картинка едет рывками.
        double decEmaUs = 0;           // среднее время разбора кадра
        double ivlEmaMs = 0;           // среднее время между кадрами
        qint64 prevAt = 0;             // когда пришёл предыдущий
        int slowRun = 0;               // подряд кадров без запаса
        bool complainedSlow = false;   // жалоба «не тянем» уже ушла
    };

    void routeCoded(quint32 sender, quint8 flags, quint8 codec, quint64 ts,
                    const QByteArray& payload);
    void emitJpeg(quint32 sender, const QByteArray& jpeg, qint64 tsMs);
    QVideoFrame convert(Peer& p, const AVFrame* f);
    void setAwaitKey(Peer& p, quint32 sender, bool awaiting);
    void setLocked(Peer& p, quint32 sender, bool locked);
    void dropDecoder(Peer& p);
    // Сказать отправителю «этот кодек нам не по силам» — не чаще раза в две
    // секунды на всю полосу: кадры идут потоком, а сообщение нужно одно.
    void complain(quint8 codec);
    // Успеваем ли за потоком; если нет — пожаловаться (см. Peer выше).
    void checkPace(Peer& p, quint8 codec, qint64 decUs, qint64 now);
    // Когда в последний раз жаловались на кодек (мс эпохи).
    qint64 m_lastCodecComplaintMs = 0;
    void sweep();                      // осиротевшие декодеры — не навсегда
    // Снять шифрование с payload. false — кадр не наш (чужой ключ): счётчик
    // подрос, а на трёх подряд плитка получит «замок».
    bool unseal(Peer& p, quint32 sender, quint8 type, quint8 codec, QByteArray& payload);

    quint8 m_codedType;
    quint8 m_jpegType;
    E2eCipher* m_cipher;               // не владеем: общий на все потоки медиа
    QHash<quint32, Peer> m_peers;      // ключ — sender из заголовка кадра
    QTimer* m_sweepTimer = nullptr;
};
