#pragma once
#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QList>

class QAudioSink;
class QTimer;
struct OpusDecoder;

class SignalingClient;
class MediaSettings;
class QAudioSource;
class QIODevice;
struct OpusEncoder;   // C-структура из opus.h — вперёд объявляется как struct

// Аудиодвижок конференции: микрофон -> Opus -> пакеты и приём (декодеры,
// джиттер-буфер, микшер). Устройства, чувствительность/громкость и битрейт
// приходят из MediaSettings (M8) и применяются на лету.
// Из QML не виден: живёт между SignalingClient и звуковым железом.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(SignalingClient* conf, MediaSettings* settings,
                         QObject* parent = nullptr);
    ~AudioEngine() override;

private:
    void onPhase();                     // фаза сменилась: live <-> остальные
    void onLocalState(bool mic, bool cam);
    void onLeft();                      // вышли из комнаты
    void onCaptured();                  // микрофон принёс порцию сэмплов
    void updateCapture();               // единственный судья: захватывать или нет
    void startCapture();
    void stopCapture();
    void restartCapture();              // смена микрофона в настройках
    void restartPlayback();             // смена динамиков (декодеры живут)

    void onJoinOk();                    // (ре)вход в комнату: буферы — мусор
    void onBinaryFrame(const QByteArray& frame);
    void onParticipantLeft(qint64 id);
    void updatePlayback();              // играем <=> мы в эфире
    void startPlayback();
    void stopPlayback();
    void resetPeers();
    void pump();                        // насос: подкормить синк миксом
    QByteArray mixOneFrame();           // один кадр микса всех участников

    SignalingClient* m_conf;            // не владеем
    MediaSettings* m_settings;          // не владеем
    bool m_live = false;                // phase == "live"
    bool m_micOn = true;                // тумблер микрофона из дока

    QAudioSource* m_source = nullptr;   // захват (жив только пока говорим)
    QIODevice* m_mic = nullptr;         // поток сэмплов (принадлежит m_source)
    QByteArray m_pcm;                   // накопитель до полного кадра
    OpusEncoder* m_enc = nullptr;       // кодер (жив вместе с захватом)

    // Приёмная сторона одного участника: декодер + джиттер-буфер.
    struct Peer {
        OpusDecoder* dec = nullptr;
        QList<QByteArray> queue;        // декодированные кадры по 1920 байт
        bool playing = false;           // предбуфер (3 кадра) пройден
    };
    QHash<quint32, Peer> m_peers;       // ключ — sender из заголовка кадра

    QAudioSink* m_sink = nullptr;
    QIODevice* m_out = nullptr;         // куда пишем микс (принадлежит m_sink)
    QTimer* m_pumpTimer = nullptr;

};