#pragma once
#include <QObject>
#include <QByteArray>
#include <QHash>

class SignalingClient;
class VideoDecoder;
class QVideoSink;
class QTimer;
struct AVFrame;
struct SwsContext;

// Приём видео (M3). Маршрутизирует VIDEO_CODED-кадры по отправителям:
// декодер на участника, ожидание keyframe, доставка в QVideoSink плитки.
// Из QML виден как "Media": плитка отдаёт свой videoSink через attach()
// и слушает videoChanged, чтобы показать/спрятать VideoOutput.
// (Имя Video занято QML-типом из QtMultimedia — поэтому Media.)
class VideoEngine : public QObject {
    Q_OBJECT
public:
    explicit VideoEngine(SignalingClient* conf, QObject* parent = nullptr);
    ~VideoEngine() override;

    Q_INVOKABLE void attach(qint64 id, QVideoSink* sink);  // плитка родилась
    Q_INVOKABLE void detach(qint64 id);                    // плитка умерла

signals:
    void videoChanged(qint64 id, bool active);  // картинка появилась/пропала

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

    SignalingClient* m_conf;           // не владеем
    QHash<quint32, Peer> m_peers;      // ключ — sender из заголовка кадра
    qint64 m_lastKeyReqAt = 0;
    QTimer* m_staleTimer = nullptr;
};
