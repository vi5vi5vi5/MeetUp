#pragma once
#include <QObject>
#include <QByteArray>

class SignalingClient;
class QAudioSource;
class QIODevice;
struct OpusEncoder;   // C-структура из opus.h — вперёд объявляется как struct

// Аудиодвижок конференции. Этап №10 — отправка: микрофон -> Opus -> пакеты.
// Приёмная половина (декодеры, джиттер-буфер, микшер) появится в №11.
// Из QML не виден: живёт между SignalingClient и звуковым железом.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(SignalingClient* conf, QObject* parent = nullptr);
    ~AudioEngine() override;

private:
    void onPhase();                     // фаза сменилась: live <-> остальные
    void onLocalState(bool mic, bool cam);
    void onLeft();                      // вышли из комнаты
    void onCaptured();                  // микрофон принёс порцию сэмплов
    void updateCapture();               // единственный судья: захватывать или нет
    void startCapture();
    void stopCapture();

    SignalingClient* m_conf;            // не владеем
    bool m_live = false;                // phase == "live"
    bool m_micOn = true;                // тумблер микрофона из дока

    QAudioSource* m_source = nullptr;   // захват (жив только пока говорим)
    QIODevice* m_mic = nullptr;         // поток сэмплов (принадлежит m_source)
    QByteArray m_pcm;                   // накопитель до полного кадра
    OpusEncoder* m_enc = nullptr;       // кодер (жив вместе с захватом)

};