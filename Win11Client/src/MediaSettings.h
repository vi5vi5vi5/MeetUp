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
    // Демонстрация экрана настраивается двумя независимыми ручками, как в
    // привычных стримерских программах: высота кадра и частота кадров.
    // screenRes: "360" | "480" | "720" | "1080" | "src" (без масштабирования).
    Q_PROPERTY(QString screenRes READ screenRes WRITE setScreenRes NOTIFY screenResChanged)
    Q_PROPERTY(int screenFps READ screenFps WRITE setScreenFps NOTIFY screenFpsChanged)
    // screenBitrate: "auto" (считается от разрешения и частоты) либо потолок в
    // кбит/с строкой — "2000", "5000", … Это именно ПОТОЛОК: регулятор качества
    // (VideoEngine::screenBitrate) вправе опуститься ниже, если канал не тянет.
    Q_PROPERTY(QString screenBitrate READ screenBitrate WRITE setScreenBitrate
               NOTIFY screenBitrateChanged)
    // Дорисовывать ли курсор при показе монитора. Захват окна курсор рисует сам
    // (другой механизм), поэтому настройка влияет только на показ монитора.
    Q_PROPERTY(bool screenCursor READ screenCursor WRITE setScreenCursor NOTIFY screenCursorChanged)
    // Кодек демонстрации: "auto" | "hevc" | "av1". У камеры выбора нет вовсе —
    // там он был бы выбором из одного правильного ответа (см. SettingsVideo.qml).
    Q_PROPERTY(QString screenCodec READ screenCodec WRITE setScreenCodec NOTIFY screenCodecChanged)
    // Передавать ли вместе с картинкой звук компьютера. По умолчанию выключено:
    // делиться звуком машины — осознанное решение, а не то, что включается само.
    Q_PROPERTY(bool screenAudio READ screenAudio WRITE setScreenAudio NOTIFY screenAudioChanged)
    // Громкость ЧУЖОЙ демонстрации (проценты, 100 = как у ведущего). Ручка
    // приёмная, а не отправляющая: громкость фонограммы у каждого своя — кто-то
    // слушает музыку, кто-то пытается расслышать за ней разговор. Раньше это
    // регулировал ведущий, и все слышали одно и то же, что бы им ни хотелось.
    Q_PROPERTY(int screenVolume READ screenVolume WRITE setScreenVolume NOTIFY screenVolumeChanged)
    // Звуки интерфейса: тумблеры, входящее сообщение, приход и уход участников.
    // По умолчанию включены — это подтверждение действия, а не украшение.
    Q_PROPERTY(bool uiSounds READ uiSounds WRITE setUiSounds NOTIFY uiSoundsChanged)
    // Горячие клавиши (M8): переносимый текст QKeySequence ("Ctrl+D", "M", …);
    // пустая строка — клавиша не назначена. Слушает ConferenceScreen.
    Q_PROPERTY(QString keyMic   READ keyMic   WRITE setKeyMic   NOTIFY keyMicChanged)
    Q_PROPERTY(QString keySound READ keySound WRITE setKeySound NOTIFY keySoundChanged)
    Q_PROPERTY(QString keyCam   READ keyCam   WRITE setKeyCam   NOTIFY keyCamChanged)
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
    QString screenRes() const { return m_screenRes; }
    int screenFps() const { return m_screenFps; }
    QString screenBitrate() const { return m_screenBitrate; }
    bool screenCursor() const { return m_screenCursor; }
    QString screenCodec() const { return m_screenCodec; }
    bool screenAudio() const { return m_screenAudio; }
    int screenVolume() const { return m_screenVolume; }
    bool uiSounds() const { return m_uiSounds; }
    QString keyMic() const { return m_keyMic; }
    QString keySound() const { return m_keySound; }
    QString keyCam() const { return m_keyCam; }
    qreal micLevel() const { return m_micLevel; }

    void setMicId(const QString& id);
    void setCamId(const QString& id);
    void setOutId(const QString& id);
    void setVolume(int v);
    void setSensitivity(int v);
    void setCamQuality(const QString& q);
    void setAudioQuality(const QString& q);
    void setScreenRes(const QString& r);
    void setScreenFps(int fps);
    void setScreenBitrate(const QString& b);
    void setScreenCursor(bool on);
    void setScreenCodec(const QString& c);
    void setScreenAudio(bool on);
    void setScreenVolume(int v);
    void setUiSounds(bool on);
    void setKeyMic(const QString& s);
    void setKeySound(const QString& s);
    void setKeyCam(const QString& s);

    // Собрать переносимый текст из клавиши и модификаторов, пришедших из
    // QML-события (Qt::Key | Qt::KeyboardModifiers). Пусто — если это одинокий
    // модификатор или зарезервированная клавиша (Esc/F11 держат полный экран).
    Q_INVOKABLE QString sequenceFromEvent(int key, int modifiers) const;

    // ---- Для движков (не QML) ----

    // Выбранное устройство; если сохранённое исчезло — системное по умолчанию.
    QAudioDevice audioInput() const;
    QAudioDevice audioOutput() const;
    QCameraDevice camera() const;

    // Пресет камеры (методичка §5.5): разрешение задаёт захват, битрейт — кодек.
    struct CamPreset { int width; int height; int fps; int bitrate; };
    CamPreset camPreset() const;
    // Пресет демонстрации экрана: разрешение — потолок (кадр вписывается в
    // рамку с сохранением пропорций), «Источник» отдаёт кадр как есть.
    // Битрейт считается от площади и частоты — вручную его никто не крутит.
    CamPreset screenPreset() const;
    int audioBitrate() const;   // бит/с для Opus

    // Гейны как множители (0..2): проценты — интерфейсу, движкам — числа.
    qreal volumeGain() const { return m_volume / 100.0; }
    qreal sensitivityGain() const { return m_sensitivity / 100.0; }
    qreal screenVolumeGain() const { return m_screenVolume / 100.0; }

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
    void screenResChanged();
    void screenFpsChanged();
    void screenBitrateChanged();
    void screenCursorChanged();
    void screenCodecChanged();
    void screenAudioChanged();
    void screenVolumeChanged();
    void uiSoundsChanged();
    void keyMicChanged();
    void keySoundChanged();
    void keyCamChanged();
    void micLevelChanged();

private:
    void save(const QString& key, const QVariant& value);

    QMediaDevices* m_devices = nullptr;   // источник сигналов о смене устройств

    QString m_micId, m_camId, m_outId;
    int m_volume = 100, m_sensitivity = 100;
    QString m_camQuality = "med", m_audioQuality = "med";
    QString m_screenRes = "720";
    int m_screenFps = 30;
    QString m_screenBitrate = "auto";
    bool m_screenCursor = true;
    QString m_screenCodec = "auto";
    bool m_screenAudio = false;
    int m_screenVolume = 100;
    bool m_uiSounds = true;
    QString m_keyMic, m_keySound, m_keyCam;

    qreal m_micLevel = 0;
    qint64 m_micLevelAt = 0;              // прореживание micLevelChanged
};
