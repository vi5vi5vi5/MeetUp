#pragma once
#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QVideoFrame>

class SignalingClient;
class MediaSettings;
class ScreenSources;
class AudioEngine;
class MediaStats;
class VideoSendWorker;
class VideoRecvWorker;
class QVideoSink;
class QVideoFrame;
class QCamera;
class QScreenCapture;
class QWindowCapture;
class QMediaCaptureSession;
class QThread;
class QTimer;

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
                         MediaStats* stats, QObject* parent = nullptr);
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
    // Вторая «дырка» под тот же поток камеры — предпросмотр в настройках.
    // Отдельная, а не та же: иначе окно настроек перехватывало бы превью у
    // self-плитки, и после закрытия та осталась бы чёрной.
    // Пока она привязана, камера снимает ДАЖЕ при выключенном тумблере —
    // именно чтобы можно было выбрать устройство и проверить кадр до эфира;
    // в сеть при этом не уходит ничего (см. onCamFrame).
    Q_INVOKABLE int attachPreviewExtra(QVideoSink* sink);
    Q_INVOKABLE void detachPreviewExtra(int token);

    // Сцена демонстрации экрана: чужая — attachScreen, своя — attachScreenPreview.
    Q_INVOKABLE int attachScreen(qint64 id, QVideoSink* sink);
    Q_INVOKABLE void detachScreen(qint64 id, int token);
    Q_INVOKABLE int attachScreenPreview(QVideoSink* sink);
    Q_INVOKABLE void detachScreenPreview(int token);

    // Текущее состояние потока — плитке при рождении. Сигнал videoChanged даёт
    // только ПЕРЕХОД, а плитку пересоздают на ровном месте (список участников в
    // QML меняется целиком, стоит кому-нибудь щёлкнуть микрофоном), и без этого
    // вопроса новая плитка сидела бы на аватарке при живом потоке кадров.
    Q_INVOKABLE bool isLive(qint64 id) const;
    Q_INVOKABLE bool isScreenLive(qint64 id) const;

    bool previewActive() const { return m_previewActive; }
    bool screenPreviewActive() const { return m_screenPreviewActive; }

signals:
    void videoChanged(qint64 id, bool active);  // картинка появилась/пропала
    void screenVideoChanged(qint64 id, bool active);
    void previewActiveChanged();
    void screenPreviewActiveChanged();
    // Захват экрана не поднялся или окно закрыли — QML показывает уведомление.
    void screenError(const QString& text);
    // Выбранный кодек не открылся, вещаем запасным — тот же тост.
    void codecNotice(const QString& text);
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

    // Приёмная сторона одного участника. Декодер и нормализатор пикселей живут
    // не здесь, а в VideoRecvWorker на своём потоке — сюда приезжает готовый
    // кадр. Тут осталось то, что умеет только GUI-поток: куда рисовать и что
    // об этом знает QML.
    struct Peer {
        QVideoSink* sink = nullptr;    // «дырка» плитки (не владеем)
        int token = 0;                 // номер текущей привязки (см. attach)
        qint64 lastFrameAt = 0;        // мс: сторож заглушки
        bool active = false;           // сейчас есть живая картинка
        // Зеркало флага воркера: поток отправителя прерван и ждёт опорного
        // кадра. Нужно ровно в одном месте — attach() по нему решает, вернуть
        // ли плитку к аватарке вместо застывшего кадра.
        bool awaitKey = true;
        QList<Held> holdQ;             // кадры, обогнавшие звук
    };

    void onBinaryFrame(const QByteArray& frame);
    void onParticipantLeft(qint64 id);
    void onJoinOk();
    void onLeft();
    void sweepStale();                 // сторож: >5 с без кадров — заглушка
    void requestKeyframe();            // KEYFRAME_REQ, не чаще 1 раза в секунду
    // Готовый кадр от воркера -> плитка. screen выбирает и полосу
    // (камера/экран), и то, какой сигнал уйдёт в QML.
    void onDecoded(QHash<quint32, Peer>& peers, quint32 sender,
                   const QVideoFrame& vf, qint64 tsMs, bool screen);
    void paint(Peer& p, quint32 sender, const QVideoFrame& vf, bool screen);
    void drainHeld();                  // отдать кадры, чьё время пришло
    void resetPeers();                 // мягкий сброс: картинка гаснет, sink'и живут
    void dropPeer(QHash<quint32, Peer>& peers, quint32 id, bool screen);
    // Плитка появилась/пропала — воркеру: декодировать в никуда незачем.
    void tellWanted(bool screen, quint32 sender, bool wanted);

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
    void applyCursorSetting();         // включить/выключить дорисовку курсора
    void applyCodecPrefs();            // выбор кодека -> обоим воркерам
    void noteCodecFallback(bool screen, int requested, int actual);
    // Кодировщик полосы открылся: какой кодек и какой кадр — в «Диагностику».
    void noteEncoderOpened(bool screen, int codec, int width, int height);

    SignalingClient* m_conf;           // не владеем
    MediaSettings* m_settings;         // не владеем
    ScreenSources* m_sources;          // не владеем: что именно снимать
    AudioEngine* m_audio;              // не владеем: спрашиваем часы звука
    MediaStats* m_stats;               // не владеем: складываем туда числа
    QHash<quint32, Peer> m_peers;      // ключ — sender из заголовка кадра
    QHash<quint32, Peer> m_screenPeers;    // та же схема для полосы экрана
    qint64 m_lastKeyReqAt = 0;
    QTimer* m_keyReqTimer = nullptr;   // просьба, отложенная ограничителем частоты
    int m_attachSeq = 0;               // выдаёт номера привязок
    int m_previewToken = 0;            // текущая привязка self-превью
    int m_scrPreviewToken = 0;         // …и превью своей демонстрации
    QTimer* m_staleTimer = nullptr;
    QTimer* m_holdTimer = nullptr;     // тикает, только пока есть придержанное

    // Приём: по потоку на полосу — см. VideoRecvWorker.
    QThread* m_recvThread = nullptr;
    VideoRecvWorker* m_recv = nullptr;         // камеры участников
    QThread* m_scrRecvThread = nullptr;
    VideoRecvWorker* m_scrRecv = nullptr;      // чужая демонстрация

    // отправка
    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_session = nullptr;
    QVideoSink* m_capSink = nullptr;   // кадры камеры прилетают сюда
    QVideoSink* m_preview = nullptr;   // self-плитка (не владеем)
    // QPointer, а не сырой указатель, как у остальных «дырок»: этот sink живёт
    // внутри раздела настроек, который пересоздаётся Loader'ом, а
    // Component.onDestruction в QML отложен — если он опоздает, писать кадр
    // будет уже некуда. QPointer сам обнулится, и запись не случится.
    QPointer<QVideoSink> m_previewExtra;    // предпросмотр в настройках (не владеем)
    int m_previewExtraToken = 0;
    QThread* m_encThread = nullptr;    // поток кодирования камеры
    VideoSendWorker* m_worker = nullptr;   // живёт на m_encThread
    int m_encInFlight = 0;             // кадров, отданных воркеру и не отработанных
    bool m_live = false;               // phase == "live"
    bool m_camOn = false;              // тумблер камеры (по умолчанию выкл.)
    bool m_previewActive = false;
    bool m_keyNext = false;            // форсировать опорный на следующем кадре
    qint64 m_lastForceAt = 0;          // rate-limit форс-keyframe (500 мс)
    // Срок следующего кадра, а не «когда был прошлый». Разница не косметическая:
    // при пороге «прошло не меньше периода» кадр, приехавший на пару миллисекунд
    // раньше срока, выбрасывался ЦЕЛИКОМ, и следующий приезжал только через
    // период — на дрожащем источнике (а захват экрана именно такой) частота
    // проваливалась вдвое. Со сроком мелкое опережение просто съедает запас.
    qint64 m_nextDueMs = 0;

    // отправка экрана
    QMediaCaptureSession* m_scrSession = nullptr;   // своя сессия: у камеры своя
    QScreenCapture* m_scrScreen = nullptr;          // выбран монитор
    QWindowCapture* m_scrWindow = nullptr;          // выбрано окно
    QVideoSink* m_scrSink = nullptr;                // кадры экрана прилетают сюда
    QVideoSink* m_scrPreview = nullptr;             // своя сцена (не владеем)
    QRect m_scrCursorRect;                          // физ. прямоугольник монитора
    // Отдельный поток, а не общий с камерой: на одном потоке кадр камеры ждал
    // бы, пока закодируется кадр экрана (а это десятки миллисекунд на 4K), и
    // приезжал бы получателю уже просроченным — с этого и разъезжались губы.
    QThread* m_scrThread = nullptr;
    VideoSendWorker* m_scrWorker = nullptr;         // живёт на m_scrThread
    int m_scrInFlight = 0;
    bool m_screenPreviewActive = false;
    bool m_scrKeyNext = false;
    qint64 m_scrNextDueMs = 0;         // см. m_nextDueMs
};
