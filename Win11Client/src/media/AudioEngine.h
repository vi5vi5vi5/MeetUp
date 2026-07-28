#pragma once
#include <QObject>

class SignalingClient;
class MediaSettings;
class MediaStats;
class AudioWorker;
class QThread;

// Аудиодвижок конференции: микрофон -> Opus -> пакеты и приём (декодеры,
// джиттер-буфер, микшер). Устройства, чувствительность/громкость и битрейт
// приходят из MediaSettings (M8) и применяются на лету.
// Из QML не виден: живёт между SignalingClient и звуковым железом.
//
// Сам звук здесь не обрабатывается — он живёт в AudioWorker на отдельном
// потоке (почему именно так, см. AudioWorker.h). Здесь остались решения: кто
// включает захват, что делать при смене устройств, что показать человеку.
// Разделение то же, что у видео: VideoEngine решает, VideoSendWorker считает.
class AudioEngine : public QObject {
    Q_OBJECT
    // «Общий звук» выключен: голоса участников не слышны (deafen). Декодеры и
    // джиттер-буфер продолжают работать — гасится только выход, поэтому
    // индикатор «говорит» и синхронизация губ не ломаются. Из QML — Audio.
    Q_PROPERTY(bool outputMuted READ outputMuted WRITE setOutputMuted
               NOTIFY outputMutedChanged)
    // Звук демонстрации сейчас РЕАЛЬНО идёт (пакеты приходили в последние ~2 с).
    // По нему сцена решает, показывать ли ручку громкости: ручка, которая ничем
    // не управляет, — это обещание звука, которого нет.
    Q_PROPERTY(bool screenAudioLive READ screenAudioLive NOTIFY screenAudioLiveChanged)
public:
    explicit AudioEngine(SignalingClient* conf, MediaSettings* settings,
                         MediaStats* stats, QObject* parent = nullptr);
    ~AudioEngine() override;

    bool outputMuted() const { return m_outputMuted; }
    void setOutputMuted(bool muted);
    bool screenAudioLive() const { return m_scrLive; }

    // Часы звука участника (мс, шкала отправителя) — по ним VideoEngine
    // придерживает обогнавшее видео. Потокобезопасно: считает воркер, здесь
    // читается снимок.
    qint64 playheadMs(quint32 id) const;

signals:
    void outputMutedChanged();
    void screenAudioLiveChanged();
    // Захват звука демонстрации не поднялся — QML показывает тост.
    void screenAudioError(const QString& text);

private:
    void onPhase();                     // фаза сменилась: live <-> остальные
    void onLocalState(bool mic, bool cam);
    void onLeft();                      // вышли из комнаты
    void updateCapture();               // единственный судья: захватывать или нет
    void updatePlayback();              // играем <=> мы в эфире
    void updateScreenAudio();           // судья: захватывать звук машины или нет
    void pushDevices();                 // выбранные устройства -> воркеру
    void pushGains();                   // громкость/чувствительность -> воркеру
    void setScreenLive(bool on);

    SignalingClient* m_conf;            // не владеем
    MediaSettings* m_settings;          // не владеем
    MediaStats* m_stats;                // не владеем: складываем туда числа
    QThread* m_thread = nullptr;        // поток звука
    AudioWorker* m_worker = nullptr;    // живёт на m_thread

    bool m_live = false;                // phase == "live"
    bool m_micOn = false;               // тумблер микрофона (по умолчанию выкл.)
    bool m_outputMuted = false;         // «общий звук» выключен (deafen)
    bool m_scrLive = false;             // звук демонстрации приходит
};
