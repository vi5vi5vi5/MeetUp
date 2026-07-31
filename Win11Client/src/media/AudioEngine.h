#pragma once
#include <QObject>
#include <QHash>

class SignalingClient;
class MediaSettings;
class ScreenSources;
class MediaStats;
class AudioWorker;
class E2eCipher;
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
    // sources — чтобы знать, показываем монитор или конкретное окно: от этого
    // зависит, чей звук снимать (см. updateScreenAudio).
    AudioEngine(SignalingClient* conf, MediaSettings* settings, ScreenSources* sources,
                MediaStats* stats, E2eCipher* cipher, QObject* parent = nullptr);
    ~AudioEngine() override;

    bool outputMuted() const { return m_outputMuted; }
    void setOutputMuted(bool muted);
    bool screenAudioLive() const { return m_scrLive; }

    // Часы звука участника (мс, шкала отправителя) — по ним VideoEngine
    // придерживает обогнавшее видео. Потокобезопасно: считает воркер, здесь
    // читается снимок.
    qint64 playheadMs(quint32 id) const;
    qint64 screenPlayheadMs(quint32 id) const;

    // Личная громкость участника, проценты 0..200 (100 — как есть). Живёт
    // только пока идёт разговор и только у нас: наружу не уходит ничего.
    // Хранить её между сессиями нельзя — у анонимов id меняется на каждом
    // переподключении, и настройка досталась бы случайному человеку.
    Q_INVOKABLE int peerVolume(qint64 id) const;
    Q_INVOKABLE void setPeerVolume(qint64 id, int percent);

signals:
    void outputMutedChanged();
    void screenAudioLiveChanged();
    // Громкость участника изменилась — плитка обновляет свой значок.
    void peerVolumeChanged(qint64 id, int percent);
    // Захват звука демонстрации не поднялся — QML показывает тост.
    void screenAudioError(const QString& text);
    // Голос участника не открывается нашим ключом (или открылся снова).
    // Слушает VideoEngine: «замок» на плитке один на все полосы участника.
    void peerLocked(qint64 id, bool locked);

private:
    void onPhase();                     // фаза сменилась: live <-> остальные
    void onLocalState(bool mic, bool cam);
    void onLeft();                      // вышли из комнаты
    void updateCapture();               // единственный судья: захватывать или нет
    void updatePlayback();              // играем <=> мы в эфире
    void updateScreenAudio();           // судья: захватывать звук машины или нет
    void pushDevices();                 // выбранные устройства -> воркеру
    void pushGains();                   // громкость/чувствительность -> воркеру
    void pushDenoise();                 // шумоподавление -> воркеру
    void forgetPeerVolumes();           // сброс личных громкостей (новая комната)
    void setScreenLive(bool on);

    SignalingClient* m_conf;            // не владеем
    MediaSettings* m_settings;          // не владеем
    ScreenSources* m_sources;           // не владеем: что именно демонстрируем
    MediaStats* m_stats;                // не владеем: складываем туда числа
    QThread* m_thread = nullptr;        // поток звука
    AudioWorker* m_worker = nullptr;    // живёт на m_thread

    bool m_live = false;                // phase == "live"
    bool m_micOn = false;               // тумблер микрофона (по умолчанию выкл.)
    bool m_outputMuted = false;         // «общий звук» выключен (deafen)
    bool m_scrLive = false;             // звук демонстрации приходит
    QHash<qint64, int> m_peerVolume;    // проценты; нет записи = 100
};
