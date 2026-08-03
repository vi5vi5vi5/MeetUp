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
    // Шумоподавление микрофона (RNNoise, см. media/Denoiser.h). По умолчанию
    // включено — ровно как noiseSuppression:true у веб-клиента: два наших
    // собственных клиента, ведущие себя при одинаковых настройках по-разному,
    // — худший вид сюрприза.
    Q_PROPERTY(bool noiseSuppression READ noiseSuppression WRITE setNoiseSuppression
               NOTIFY noiseSuppressionChanged)
    // Автоусиление микрофона (см. media/AutoGain.h). По умолчанию включено —
    // как autoGainControl:true у веб-клиента, по той же причине, что и выше.
    // Работает ТОЛЬКО вместе с шумоподавлением, поэтому читается не сама
    // настройка, а «выбрано и разрешено»; подробности у autoGain().
    Q_PROPERTY(bool autoGain READ autoGain WRITE setAutoGain NOTIFY autoGainChanged)
    // Множитель, который СЕЙЧАС держит автоусиление, в тех же процентах, что и
    // ползунок чувствительности. Только чтение и только в памяти: значение
    // живёт двадцать миллисекунд, и хранить его между запусками так же
    // бессмысленно, как хранить показание вольтметра.
    Q_PROPERTY(int agcSensitivity READ agcSensitivity NOTIFY agcSensitivityChanged)
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
    // Кодек полосы: "auto" либо имя кодировщика из VideoEncoder::catalog()
    // ("hevc_mf", "libvpx-vp9"…). Это ПРЕДПОЧТЕНИЕ, а не приказ: выбранный
    // пробуется первым, но если он не открылся или получатель его не понял,
    // работает обычная лестница — см. VideoEncoder::open. Проверять значение
    // здесь незачем: незнакомое имя просто не найдётся в каталоге и молча
    // означает «Авто».
    Q_PROPERTY(QString screenCodec READ screenCodec WRITE setScreenCodec
               NOTIFY screenCodecChanged)
    Q_PROPERTY(QString camCodec READ camCodec WRITE setCamCodec NOTIFY camCodecChanged)
    // Буферы конвейера. Включены — как было всегда: кадр, который не успели
    // обработать, ждёт своей очереди. Выключены — такой кадр выбрасывается на
    // месте, и отставание не может накопиться в принципе.
    //
    // Настройка нужна потому, что правильный ответ зависит от того, что важнее
    // здесь и сейчас. Буфер сглаживает рывки на неровной сети и на тяжёлом
    // кодеке; он же превращает разовую заминку в постоянное опоздание, которое
    // уже не рассасывается — его видно, когда переключаешь кодек, а зритель
    // ещё полминуты досматривает предыдущий.
    Q_PROPERTY(bool rxBuffer READ rxBuffer WRITE setRxBuffer NOTIFY rxBufferChanged)
    Q_PROPERTY(bool txBuffer READ txBuffer WRITE setTxBuffer NOTIFY txBufferChanged)
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
    // Горячие клавиши (M8): текст бинда в собственном формате — "RCtrl",
    // "RCtrl+RShift", "Ctrl+D", "F9" (разбор и правила в src/HotkeySpec.h).
    // Пустая строка — клавиша не назначена. Слушают GlobalHotkeys и настройки.
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
    bool noiseSuppression() const { return m_noiseSuppression; }
    // Выбор человека И разрешение его применить. Без шумоподавления усиливать
    // пришлось бы фон вместе с голосом, и автоусиление упирается в собственный
    // предохранитель по шумовой полке, то есть почти ничего не делает —
    // тумблер, который включается и не работает, хуже недоступного.
    // Сам выбор при этом не переписывается: вернут шумодав — вернётся и оно.
    bool autoGain() const { return m_autoGain && m_noiseSuppression; }
    int agcSensitivity() const { return m_agcSensitivity; }
    QString camQuality() const { return m_camQuality; }
    QString audioQuality() const { return m_audioQuality; }
    QString screenRes() const { return m_screenRes; }
    int screenFps() const { return m_screenFps; }
    QString screenBitrate() const { return m_screenBitrate; }
    bool screenCursor() const { return m_screenCursor; }
    QString screenCodec() const { return m_screenCodec; }
    void setScreenCodec(const QString& id);
    QString camCodec() const { return m_camCodec; }
    void setCamCodec(const QString& id);
    bool rxBuffer() const { return m_rxBuffer; }
    void setRxBuffer(bool on);
    bool txBuffer() const { return m_txBuffer; }
    void setTxBuffer(bool on);
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
    void setNoiseSuppression(bool on);
    void setAutoGain(bool on);
    void setCamQuality(const QString& q);
    void setAudioQuality(const QString& q);
    void setScreenRes(const QString& r);
    void setScreenFps(int fps);
    void setScreenBitrate(const QString& b);
    void setScreenCursor(bool on);
    void setScreenAudio(bool on);
    void setScreenVolume(int v);
    void setUiSounds(bool on);
    void setKeyMic(const QString& s);
    void setKeySound(const QString& s);
    void setKeyCam(const QString& s);

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
    // …и множитель автоусиления (1.0 = «как есть»). Прореживание здесь не
    // нужно — оно уже сделано на потоке звука, где известно, сколько прошло
    // кадров; сюда приходит готовое значение примерно раз в 100 мс.
    void reportAgcGain(qreal gain);

signals:
    void devicesChanged();
    void micIdChanged();
    void camIdChanged();
    void outIdChanged();
    void volumeChanged();
    void sensitivityChanged();
    void noiseSuppressionChanged();
    void autoGainChanged();
    void agcSensitivityChanged();
    void camQualityChanged();
    void audioQualityChanged();
    void screenResChanged();
    void screenFpsChanged();
    void screenBitrateChanged();
    void screenCursorChanged();
    void screenCodecChanged();
    void camCodecChanged();
    void rxBufferChanged();
    void txBufferChanged();
    void screenAudioChanged();
    void screenVolumeChanged();
    void uiSoundsChanged();
    void keyMicChanged();
    void keySoundChanged();
    void keyCamChanged();
    void micLevelChanged();

private:
    void save(const QString& key, const QVariant& value);
    // Автоусиление погасло — ползунок должен показать ручное значение, а не
    // застыть на последнем, что насчитал AutoGain.
    void resetAgcReadout();

    QMediaDevices* m_devices = nullptr;   // источник сигналов о смене устройств

    QString m_micId, m_camId, m_outId;
    int m_volume = 100, m_sensitivity = 100;
    bool m_noiseSuppression = true;
    bool m_autoGain = true;
    QString m_camQuality = "med", m_audioQuality = "med";
    QString m_screenRes = "720";
    int m_screenFps = 30;
    QString m_screenBitrate = "auto";
    bool m_screenCursor = true;
    QString m_screenCodec = "auto";
    QString m_camCodec = "auto";
    bool m_rxBuffer = true;
    bool m_txBuffer = true;
    bool m_screenAudio = false;
    int m_screenVolume = 100;
    bool m_uiSounds = true;
    QString m_keyMic, m_keySound, m_keyCam;

    qreal m_micLevel = 0;
    qint64 m_micLevelAt = 0;              // прореживание micLevelChanged
    int m_agcSensitivity = 100;           // проценты; в QSettings не попадает
};
