#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QAudioDevice>
#include <QCameraDevice>

class QMediaDevices;

// Настройки медиа (M8): выбор устройств ввода-вывода, громкость/чувствительность,
// пресеты качества отправки. Одна точка правды: QML-модалка настроек пишет сюда,
// Audio/VideoEngine слушают сигналы и перезапускают захват/воспроизведение.
// Значения переживают перезапуск (QSettings), как localStorage у веба.
class MediaSettings : public QObject {
    Q_OBJECT
    // Списки устройств: [{id, label}]. Пустой id = «системное по умолчанию».
    Q_PROPERTY(QVariantList micDevices READ micDevices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList camDevices READ camDevices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList outDevices READ outDevices NOTIFY devicesChanged)
    // Выбранные устройства (id из списков выше; "" = системное).
    Q_PROPERTY(QString micId READ micId WRITE setMicId NOTIFY micIdChanged)
    Q_PROPERTY(QString camId READ camId WRITE setCamId NOTIFY camIdChanged)
    Q_PROPERTY(QString outId READ outId WRITE setOutId NOTIFY outIdChanged)
    // Проценты 0..200, как у веба (100 = как есть).
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(int sensitivity READ sensitivity WRITE setSensitivity NOTIFY sensitivityChanged)
    // Пресеты качества отправки: "low" | "med" | "high".
    Q_PROPERTY(QString camQuality READ camQuality WRITE setCamQuality NOTIFY camQualityChanged)
    Q_PROPERTY(QString audioQuality READ audioQuality WRITE setAudioQuality NOTIFY audioQualityChanged)
    // Уровень микрофона 0..1 (RMS) — индикатор в настройках. Пишет AudioEngine.
    Q_PROPERTY(qreal micLevel READ micLevel NOTIFY micLevelChanged)
public:
    explicit MediaSettings(QObject* parent = nullptr);

    QVariantList micDevices() const;
    QVariantList camDevices() const;
    QVariantList outDevices() const;

    QString micId() const { return m_micId; }
    QString camId() const { return m_camId; }
    QString outId() const { return m_outId; }
    int volume() const { return m_volume; }
    int sensitivity() const { return m_sensitivity; }
    QString camQuality() const { return m_camQuality; }
    QString audioQuality() const { return m_audioQuality; }
    qreal micLevel() const { return m_micLevel; }

    void setMicId(const QString& id);
    void setCamId(const QString& id);
    void setOutId(const QString& id);
    void setVolume(int v);
    void setSensitivity(int v);
    void setCamQuality(const QString& q);
    void setAudioQuality(const QString& q);

    // ---- Для движков (не QML) ----

    // Выбранное устройство; если сохранённое исчезло — системное по умолчанию.
    QAudioDevice audioInput() const;
    QAudioDevice audioOutput() const;
    QCameraDevice camera() const;

    // Пресет камеры (методичка §5.5): разрешение задаёт захват, битрейт — кодек.
    struct CamPreset { int width; int height; int fps; int bitrate; };
    CamPreset camPreset() const;
    int audioBitrate() const;   // бит/с для Opus

    // Гейны как множители (0..2): проценты — интерфейсу, движкам — числа.
    qreal volumeGain() const { return m_volume / 100.0; }
    qreal sensitivityGain() const { return m_sensitivity / 100.0; }

    // AudioEngine сообщает RMS захвата; уведомления QML прорежены до ~10 Гц.
    void reportMicLevel(qreal level);

signals:
    void devicesChanged();
    void micIdChanged();
    void camIdChanged();
    void outIdChanged();
    void volumeChanged();
    void sensitivityChanged();
    void camQualityChanged();
    void audioQualityChanged();
    void micLevelChanged();

private:
    void save(const QString& key, const QVariant& value);

    QMediaDevices* m_devices = nullptr;   // источник сигналов о смене устройств

    QString m_micId, m_camId, m_outId;
    int m_volume = 100, m_sensitivity = 100;
    QString m_camQuality = "med", m_audioQuality = "med";

    qreal m_micLevel = 0;
    qint64 m_micLevelAt = 0;              // прореживание micLevelChanged
};
