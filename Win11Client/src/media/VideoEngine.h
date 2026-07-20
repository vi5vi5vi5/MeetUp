#pragma once
#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QVideoFrame>

class SignalingClient;
class MediaSettings;
class ScreenSources;
class AudioEngine;
class VideoDecoder;
class VideoSendWorker;
class QVideoSink;
class QVideoFrame;
class QCamera;
class QScreenCapture;
class QWindowCapture;
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
    // Свой захват экрана реально даёт кадры — сцена показывает превью.
    Q_PROPERTY(bool screenPreviewActive READ screenPreviewActive
               NOTIFY screenPreviewActiveChanged)
public:
    explicit VideoEngine(SignalingClient* conf, MediaSettings* settings,
                         ScreenSources* sources, AudioEngine* audio,
                         QObject* parent = nullptr);
    ~VideoEngine() override;

    // Плитка родилась: возвращает НОМЕР привязки, который она обязана вернуть
    // при смерти. Именно номер, а не указатель на sink: при смене раскладки
    // (сетка <-> сцена, страницы) новая плитка успевает сделать attach раньше,
    // чем старая — detach, а Component.onDestruction в QML вообще отложен.
    // Указатель к тому времени либо недоступен, либо (хуже) новый sink лёг по
    // тому же адресу — и старая плитка обнуляла бы живую привязку, после чего
    // видео не возвращалось никогда.
    Q_INVOKABLE int attach(qint64 id, QVideoSink* sink);
    Q_INVOKABLE void detach(qint64 id, int token);
    Q_INVOKABLE int attachPreview(QVideoSink* sink);       // self-плитка
    Q_INVOKABLE void detachPreview(int token);

    // Сцена демонстрации экрана: чужая — attachScreen, своя — attachScreenPreview.
    Q_INVOKABLE int attachScreen(qint64 id, QVideoSink* sink);
    Q_INVOKABLE void detachScreen(qint64 id, int token);
    Q_INVOKABLE int attachScreenPreview(QVideoSink* sink);
    Q_INVOKABLE void detachScreenPreview(int token);

    bool previewActive() const { return m_previewActive; }
    bool screenPreviewActive() const { return m_screenPreviewActive; }

signals:
    void videoChanged(qint64 id, bool active);  // картинка появилась/пропала
    void screenVideoChanged(qint64 id, bool active);
    void previewActiveChanged();
    void screenPreviewActiveChanged();
    // Захват экрана не поднялся или окно закрыли — QML показывает уведомление.
    void screenError(const QString& text);
    // Кадр камеры -> воркеру на кодирующем потоке (queued). Тяжёлый sws+encode
    // уходит с GUI-потока, чтобы окно не дёргалось при перетаскивании.
    void frameToEncode(const QVideoFrame& frame, int maxW, int maxH, int fps,
                       int bitrate, bool forceKey, qint64 tsMs);
    // То же для полосы экрана — у неё свой воркер со своим энкодером.
    void screenFrameToEncode(const QVideoFrame& frame, int maxW, int maxH, int fps,
                             int bitrate, bool forceKey, qint64 tsMs);

private:
    // Кадр, придержанный до своего звука (синхронизация губ).
    struct Held {
        QVideoFrame frame;
        qint64 ts = 0;                 // метка отправителя, мс
    };

    // Приёмная сторона одного участника.
    struct Peer {
        VideoDecoder* dec = nullptr;   // создаётся под первый кадр
        SwsContext* sws = nullptr;     // нормализатор пикселей (кэшируется)
        bool awaitKey = true;          // дельты без опорного кадра — мусор
        QVideoSink* sink = nullptr;    // «дырка» плитки (не владеем)
        int token = 0;                 // номер текущей привязки (см. attach)
        qint64 lastFrameAt = 0;        // мс: сторож заглушки
        bool active = false;           // сейчас есть живая картинка
        QList<Held> holdQ;             // кадры, обогнавшие звук
    };

    void onBinaryFrame(const QByteArray& frame);
    void onParticipantLeft(qint64 id);
    void onJoinOk();
    void onLeft();
    void sweepStale();                 // сторож: >5 с без кадров — заглушка
    void requestKeyframe();            // KEYFRAME_REQ, не чаще 1 раза в секунду
    // Кадр кодека -> декодер нужного участника. screen выбирает и полосу
    // (камера/экран), и то, какой сигнал уйдёт в QML.
    void routeCoded(QHash<quint32, Peer>& peers, quint32 sender, quint8 flags,
                    quint8 codec, quint64 ts, const QByteArray& payload, bool screen);
    void deliver(Peer& p, quint32 sender, const AVFrame* f, qint64 tsMs, bool screen);
    void paint(Peer& p, quint32 sender, const QVideoFrame& vf, bool screen);
    void drainHeld();                  // отдать кадры, чьё время пришло
    void paintJpeg(Peer& p, quint32 sender, const QByteArray& jpeg, bool screen);
    void resetPeers();                 // мягкий сброс: декодеры в мусор, sink'и живут
    void dropPeer(QHash<quint32, Peer>& peers, quint32 id, bool screen);

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

    // ---- демонстрация экрана (отправка) ----
    void onScreenSlotChanged();        // сервер сказал, кто ведёт демонстрацию
    void startScreenCapture();
    void stopScreenCapture();
    void restartScreenCapture();       // смена пресета качества на лету
    void onScreenCapFrame(const QVideoFrame& frame);
    void failScreen(const QString& text);   // сорвалось: отпустить слот и сказать
    void setScreenPreviewActive(bool on);

    SignalingClient* m_conf;           // не владеем
    MediaSettings* m_settings;         // не владеем
    ScreenSources* m_sources;          // не владеем: что именно снимать
    AudioEngine* m_audio;              // не владеем: спрашиваем часы звука
    QHash<quint32, Peer> m_peers;      // ключ — sender из заголовка кадра
    QHash<quint32, Peer> m_screenPeers;    // та же схема для полосы экрана
    qint64 m_lastKeyReqAt = 0;
    QTimer* m_keyReqTimer = nullptr;   // просьба, отложенная ограничителем частоты
    int m_attachSeq = 0;               // выдаёт номера привязок
    int m_previewToken = 0;            // текущая привязка self-превью
    int m_scrPreviewToken = 0;         // …и превью своей демонстрации
    QTimer* m_staleTimer = nullptr;
    QTimer* m_holdTimer = nullptr;     // тикает, только пока есть придержанное

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

    // отправка экрана
    QMediaCaptureSession* m_scrSession = nullptr;   // своя сессия: у камеры своя
    QScreenCapture* m_scrScreen = nullptr;          // выбран монитор
    QWindowCapture* m_scrWindow = nullptr;          // выбрано окно
    QVideoSink* m_scrSink = nullptr;                // кадры экрана прилетают сюда
    QVideoSink* m_scrPreview = nullptr;             // своя сцена (не владеем)
    VideoSendWorker* m_scrWorker = nullptr;         // живёт на m_encThread
    bool m_screenPreviewActive = false;
    bool m_scrKeyNext = false;
    qint64 m_scrLastEncodeAt = 0;
};
