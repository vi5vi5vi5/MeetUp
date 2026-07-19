#pragma once
#include <QObject>
#include <QByteArray>
#include <QHash>

class SignalingClient;
class MediaSettings;
class VideoDecoder;
class VideoSendWorker;
class QVideoSink;
class QVideoFrame;
class QCamera;
class QMediaCaptureSession;
class QThread;
class QTimer;
struct AVFrame;
struct SwsContext;

// Видеодвижок. Приём (M3): маршрутизирует VIDEO_CODED-кадры по отправителям —
// декодер на участника, ожидание keyframe, доставка в QVideoSink плитки.
// Отправка (M4): камера (QCamera по выбору из настроек) -> YUV420P ->
// H.264/VP8 -> пакеты v2; keyframe каждые ~72 кадра, по KEYFRAME_REQ и на
// входе новичка; backpressure — пропуск кадров при заторе в сокете.
// Из QML виден как "Media": плитка отдаёт свой videoSink через attach()
// (своя — через attachPreview) и слушает videoChanged/previewActive.
// (Имя Video занято QML-типом из QtMultimedia — поэтому Media.)
class VideoEngine : public QObject {
    Q_OBJECT
    // Локальная камера реально даёт кадры — self-плитка показывает превью.
    Q_PROPERTY(bool previewActive READ previewActive NOTIFY previewActiveChanged)
public:
    explicit VideoEngine(SignalingClient* conf, MediaSettings* settings,
                         QObject* parent = nullptr);
    ~VideoEngine() override;

    Q_INVOKABLE void attach(qint64 id, QVideoSink* sink);  // плитка родилась
    // Плитка умерла. sink передаём, чтобы отцепить ТОЛЬКО свою привязку:
    // при смене раскладки (сетка <-> сцена, страницы) новая плитка успевает
    // сделать attach раньше, чем старая — detach, и без этой проверки старая
    // обнулила бы уже чужой sink, и видео пропадало бы насовсем.
    Q_INVOKABLE void detach(qint64 id, QVideoSink* sink = nullptr);
    Q_INVOKABLE void attachPreview(QVideoSink* sink);      // self-плитка
    Q_INVOKABLE void detachPreview();

    bool previewActive() const { return m_previewActive; }

signals:
    void videoChanged(qint64 id, bool active);  // картинка появилась/пропала
    void previewActiveChanged();
    // Кадр камеры -> воркеру на кодирующем потоке (queued). Тяжёлый sws+encode
    // уходит с GUI-потока, чтобы окно не дёргалось при перетаскивании.
    void frameToEncode(const QVideoFrame& frame, int maxW, int maxH, int fps,
                       int bitrate, bool forceKey, qint64 tsMs);

private:
    // Приёмная сторона одного участника.
    struct Peer {
        VideoDecoder* dec = nullptr;   // создаётся под первый кадр
        SwsContext* sws = nullptr;     // нормализатор пикселей (кэшируется)
        bool awaitKey = true;          // дельты без опорного кадра — мусор
        QVideoSink* sink = nullptr;    // «дырка» плитки (не владеем)
        qint64 lastFrameAt = 0;        // мс: сторож заглушки
        bool active = false;           // сейчас есть живая картинка
    };

    void onBinaryFrame(const QByteArray& frame);
    void onParticipantLeft(qint64 id);
    void onJoinOk();
    void onLeft();
    void sweepStale();                 // сторож: >5 с без кадров — заглушка
    void requestKeyframe();            // KEYFRAME_REQ, не чаще 1 раза в секунду
    void deliver(Peer& p, quint32 sender, const AVFrame* f);
    void paintJpeg(Peer& p, quint32 sender, const QByteArray& jpeg);
    void resetPeers();                 // мягкий сброс: декодеры в мусор, sink'и живут

    // ---- отправка ----
    void onPhase();
    void onLocalState(bool mic, bool cam);
    void updateCapture();              // единственный судья: снимать или нет
    void startCapture();
    void stopCapture();
    void restartCapture();             // смена камеры/качества на лету
    void onCamFrame(const QVideoFrame& frame);
    void forceKeyframe();              // пометить следующий кадр опорным (≤1 в 500 мс)
    void setPreviewActive(bool on);

    SignalingClient* m_conf;           // не владеем
    MediaSettings* m_settings;         // не владеем
    QHash<quint32, Peer> m_peers;      // ключ — sender из заголовка кадра
    qint64 m_lastKeyReqAt = 0;
    QTimer* m_staleTimer = nullptr;

    // отправка
    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_session = nullptr;
    QVideoSink* m_capSink = nullptr;   // кадры камеры прилетают сюда
    QVideoSink* m_preview = nullptr;   // self-плитка (не владеем)
    QThread* m_encThread = nullptr;    // поток кодирования
    VideoSendWorker* m_worker = nullptr;   // живёт на m_encThread
    bool m_live = false;               // phase == "live"
    bool m_camOn = true;               // тумблер камеры из дока
    bool m_previewActive = false;
    bool m_keyNext = false;            // форсировать опорный на следующем кадре
    qint64 m_lastForceAt = 0;          // rate-limit форс-keyframe (500 мс)
    qint64 m_lastEncodeAt = 0;         // троттлинг до fps пресета
};
