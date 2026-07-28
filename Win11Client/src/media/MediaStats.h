#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QSet>
#include <QString>

class SignalingClient;
class QTimer;

// Что на самом деле происходит с медиа — в числах. Раздел «Диагностика» без
// этого объекта умел показать только задержку: остальное движки считали, но
// наружу не отдавали, и разбор жалобы «меня не слышно» начинался с просьбы
// прислать лог. Счётчики копятся там, где события происходят, и раз в секунду
// превращаются в скорости. В QML — Stats.
//
// Всё складывается на GUI-потоке: числа от воркеров (кодирование, звук)
// приезжают сюда queued-сигналами, а входящие байты забираются раз в секунду из
// атомика транспорта. Поэтому здесь ни своих атомиков, ни мьютексов.
class MediaStats : public QObject {
    Q_OBJECT
    // Приём — суммарно по всем полосам: голос, камера, демонстрация.
    Q_PROPERTY(int rxKbps READ rxKbps NOTIFY updated)
    Q_PROPERTY(int rxFps READ rxFps NOTIFY updated)
    Q_PROPERTY(int rxStreams READ rxStreams NOTIFY updated)
    // Отправка. Голос — микрофон; демонстрация считается вместе со своим
    // звуком: платим-то мы за неё целиком.
    Q_PROPERTY(int txVoiceKbps READ txVoiceKbps NOTIFY updated)
    Q_PROPERTY(int txCamKbps READ txCamKbps NOTIFY updated)
    Q_PROPERTY(int txCamFps READ txCamFps NOTIFY updated)
    Q_PROPERTY(QString txCamFormat READ txCamFormat NOTIFY updated)
    Q_PROPERTY(int txScrKbps READ txScrKbps NOTIFY updated)
    Q_PROPERTY(int txScrFps READ txScrFps NOTIFY updated)
    Q_PROPERTY(QString txScrFormat READ txScrFormat NOTIFY updated)
    Q_PROPERTY(bool txScrAudio READ txScrAudio NOTIFY updated)
    // Кадры, которые сняли, но не отправили: затор в сокете или кодировщик,
    // не поспевающий за камерой. Это и есть честная замена «потерям пакетов»,
    // которых у нас нет: транспорт TCP, по дороге не теряется ничего — теряем
    // мы сами, до отправки.
    Q_PROPERTY(int camDropPercent READ camDropPercent NOTIFY updated)
    Q_PROPERTY(int scrDropPercent READ scrDropPercent NOTIFY updated)
    // На сколько миллисекунд видео придерживается, чтобы совпасть со звуком.
    Q_PROPERTY(int syncHoldMs READ syncHoldMs NOTIFY updated)
public:
    explicit MediaStats(SignalingClient* conf, QObject* parent = nullptr);

    int rxKbps() const { return m_rxKbps; }
    int rxFps() const { return m_rxFps; }
    int rxStreams() const { return m_rxStreams; }
    int txVoiceKbps() const { return m_voice.kbps; }
    int txCamKbps() const { return m_cam.kbps; }
    int txCamFps() const { return m_cam.fps; }
    QString txCamFormat() const { return m_cam.format; }
    int txScrKbps() const { return m_scr.kbps; }
    int txScrFps() const { return m_scr.fps; }
    QString txScrFormat() const { return m_scr.format; }
    bool txScrAudio() const { return m_scrAudio; }
    int camDropPercent() const { return m_cam.dropPercent; }
    int scrDropPercent() const { return m_scr.dropPercent; }
    int syncHoldMs() const { return m_syncHoldMs; }

    // ---- со стороны движков ----
    void noteTxVideo(bool screen, int bytes);
    void noteTxAudio(bool screen, int bytes);   // screen = звук демонстрации
    void noteTxAttempt(bool screen);            // кадр снят и должен был уйти
    void noteTxDrop(bool screen);               // …но не ушёл
    void noteEncoder(bool screen, const QString& codec, int w, int h);
    void noteTxOff(bool screen);                // полоса погасла: числа стереть
    void noteRxFrame(quint32 sender);           // отрисован входящий кадр
    void noteSyncHold(qint64 ms);               // видео придержано под звук

signals:
    // Один сигнал на все свойства: они пересчитываются одним тиком, и плодить
    // четырнадцать NOTIFY ради этого незачем.
    void updated();

private:
    // Одна полоса отправки.
    struct Band {
        qint64 bytes = 0;      // ушло за окно
        int attempts = 0;      // кадров снято и предложено к отправке
        int drops = 0;         // из них выброшено до кодирования
        // готовые значения
        int kbps = 0, fps = 0, dropPercent = 0;
        QString format;        // "H.264 · 1280×720"
    };

    void tick();
    bool isQuiet() const;
    Band& band(bool screen) { return screen ? m_scr : m_cam; }

    SignalingClient* m_conf;       // не владеем: у него счётчик входящих байт
    Band m_cam, m_scr, m_voice;
    bool m_scrAudio = false, m_scrAudioSeen = false;

    int m_rxFrames = 0;
    QSet<quint32> m_rxSenders;     // сколько разных потоков рисовалось за окно
    int m_rxKbps = 0, m_rxFps = 0, m_rxStreams = 0;

    qint64 m_holdSum = 0;
    int m_holdCount = 0;
    int m_syncHoldMs = 0;

    QElapsedTimer m_window;        // сколько реально длилось окно
    QTimer* m_timer = nullptr;
};
