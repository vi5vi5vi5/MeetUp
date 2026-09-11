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
    // Эхоподавление микрофона (SpeexDSP, см. media/EchoCanceller.h). По
    // умолчанию включено — как echoCancellation:true у веб-клиента. В наушниках
    // не мешает: вычитать нечего, и фильтр это видит сам.
    Q_PROPERTY(bool echoCancel READ echoCancel WRITE setEchoCancel NOTIFY echoCancelChanged)
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
    // Буфер отправки. Включён — как было всегда: снятый кадр, который не
    // успели обработать, ждёт своей очереди. Выключен — такой кадр
    // выбрасывается на месте, и отставание не может накопиться в принципе.
    // Буфер сглаживает рывки; он же превращает разовую заминку интерфейса в
    // постоянное опоздание демонстрации от того, что человек видит у себя.
    Q_PROPERTY(bool txBuffer READ txBuffer WRITE setTxBuffer NOTIFY txBufferChanged)
    // Приём устроен иначе: выключателя у его буфера нет. Без буфера картинка
    // рвётся всегда — даже когда в очереди набирается один кадр (проверено:
    // каждый выброшенный кадр рвёт поток и стоит паузы до опорного). Вместо
    // выключателя — авто-сброс: порог отставания в миллисекундах, при котором
    // очередь приёма выбрасывается целиком и картинка перескакивает на живой
    // край (см. VideoRecvWorker::onFrame). 0 — выключен; иначе 300..10000.
    Q_PROPERTY(int rxAutoResetMs READ rxAutoResetMs WRITE setRxAutoResetMs
               NOTIFY rxAutoResetMsChanged)
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
    // Зеркалить своё видео — и в плитке, и на сцене, и в предпросмотре камеры.
    // По умолчанию включено: человек привык видеть себя в зеркале, и
    // неотзеркаленное лицо читается как чужое. Настройка чисто своя: в эфир
    // кадр уходит как есть, собеседники ничего не заметят.
    Q_PROPERTY(bool mirrorSelf READ mirrorSelf WRITE setMirrorSelf NOTIFY mirrorSelfChanged)
    // Сетка участников — как perPage / showSelf / hideNoVideo у веба.
    // perPage — плиток на странице сетки (6 | 9 | 12); showSelf — своя плитка
    // в сетке; hideNoVideo — прятать участников с выключенной камерой, чтобы
    // в большой комнате на экране остались лица. Панель участников эти
    // фильтры не трогают.
    Q_PROPERTY(int perPage READ perPage WRITE setPerPage NOTIFY perPageChanged)
    Q_PROPERTY(bool showSelf READ showSelf WRITE setShowSelf NOTIFY showSelfChanged)
    Q_PROPERTY(bool hideNoVideo READ hideNoVideo WRITE setHideNoVideo NOTIFY hideNoVideoChanged)
    // Горячие клавиши (M8): текст бинда в собственном формате — "RCtrl",
    // "RCtrl+RShift", "Ctrl+D", "F9" (разбор и правила в src/HotkeySpec.h).
    // Пустая строка — клавиша не назначена. Слушают GlobalHotkeys и настройки.
    Q_PROPERTY(QString keyMic   READ keyMic   WRITE setKeyMic   NOTIFY keyMicChanged)
    Q_PROPERTY(QString keySound READ keySound WRITE setKeySound NOTIFY keySoundChanged)
    Q_PROPERTY(QString keyCam   READ keyCam   WRITE setKeyCam   NOTIFY keyCamChanged)
    Q_PROPERTY(QString keyShare READ keyShare WRITE setKeyShare NOTIFY keyShareChanged)
    Q_PROPERTY(QString keyFull  READ keyFull  WRITE setKeyFull  NOTIFY keyFullChanged)
    Q_PROPERTY(QString keyLeave READ keyLeave WRITE setKeyLeave NOTIFY keyLeaveChanged)
    // Рация: клавиша микрофона перестаёт быть выключателем и работает на
    // удержание — микрофон открыт, пока она зажата, и закрывается сразу на
    // отпускании. Механизм — в GlobalHotkeys (сырой ввод сообщает и о нажатии,
    // и об отпускании, чего RegisterHotKey не умел).
    Q_PROPERTY(bool pushToTalk READ pushToTalk WRITE setPushToTalk NOTIFY pushToTalkChanged)
    // Режим разработчика: показывает служебные настройки, которым не место
    // перед обычным человеком, — кодеки, буферы конвейера, диагностику, ещё не
    // подключённые тумблеры. Живёт здесь, а не в отдельном объекте, потому что
    // это ровно то же самое: строчка в файле настроек, которую читает QML.
    Q_PROPERTY(bool devMode READ devMode WRITE setDevMode NOTIFY devModeChanged)
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
    bool echoCancel() const { return m_echoCancel; }
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
    bool txBuffer() const { return m_txBuffer; }
    void setTxBuffer(bool on);
    int rxAutoResetMs() const { return m_rxAutoResetMs; }
    void setRxAutoResetMs(int ms);
    bool screenAudio() const { return m_screenAudio; }
    int screenVolume() const { return m_screenVolume; }
    bool uiSounds() const { return m_uiSounds; }
    bool mirrorSelf() const { return m_mirrorSelf; }
    int perPage() const { return m_perPage; }
    bool showSelf() const { return m_showSelf; }
    bool hideNoVideo() const { return m_hideNoVideo; }
    QString keyMic() const { return m_keyMic; }
    QString keySound() const { return m_keySound; }
    QString keyCam() const { return m_keyCam; }
    QString keyShare() const { return m_keyShare; }
    QString keyFull() const { return m_keyFull; }
    QString keyLeave() const { return m_keyLeave; }
    // Рация без назначенной клавиши микрофона — режим без органа управления:
    // микрофон в нём не открылся бы никогда. Сам выбор при этом не
    // переписывается: назначат клавишу — рация и заработает (ср. autoGain()).
    bool pushToTalk() const { return m_pushToTalk && !m_keyMic.isEmpty(); }
    bool devMode() const { return m_devMode; }
    qreal micLevel() const { return m_micLevel; }

    void setMicId(const QString& id);
    void setCamId(const QString& id);
    void setOutId(const QString& id);
    void setVolume(int v);
    void setSensitivity(int v);
    void setNoiseSuppression(bool on);
    void setAutoGain(bool on);
    void setEchoCancel(bool on);
    void setCamQuality(const QString& q);
    void setAudioQuality(const QString& q);
    void setScreenRes(const QString& r);
    void setScreenFps(int fps);
    void setScreenBitrate(const QString& b);
    void setScreenCursor(bool on);
    void setScreenAudio(bool on);
    void setScreenVolume(int v);
    void setUiSounds(bool on);
    void setMirrorSelf(bool on);
    void setPerPage(int n);
    void setShowSelf(bool on);
    void setHideNoVideo(bool on);
    void setKeyMic(const QString& s);
    void setKeySound(const QString& s);
    void setKeyCam(const QString& s);
    void setKeyShare(const QString& s);
    void setKeyFull(const QString& s);
    void setKeyLeave(const QString& s);
    void setPushToTalk(bool on);
    void setDevMode(bool on);

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
    void echoCancelChanged();
    void agcSensitivityChanged();
    void camQualityChanged();
    void audioQualityChanged();
    void screenResChanged();
    void screenFpsChanged();
    void screenBitrateChanged();
    void screenCursorChanged();
    void screenCodecChanged();
    void camCodecChanged();
    void txBufferChanged();
    void rxAutoResetMsChanged();
    void screenAudioChanged();
    void screenVolumeChanged();
    void uiSoundsChanged();
    void mirrorSelfChanged();
    void perPageChanged();
    void showSelfChanged();
    void hideNoVideoChanged();
    void keyMicChanged();
    void keySoundChanged();
    void keyCamChanged();
    void keyShareChanged();
    void keyFullChanged();
    void keyLeaveChanged();
    void pushToTalkChanged();
    void devModeChanged();
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
    bool m_echoCancel = true;
    QString m_camQuality = "med", m_audioQuality = "med";
    QString m_screenRes = "720";
    int m_screenFps = 30;
    QString m_screenBitrate = "auto";
    bool m_screenCursor = true;
    QString m_screenCodec = "auto";
    QString m_camCodec = "auto";
    bool m_txBuffer = true;
    int m_rxAutoResetMs = 1000;
    bool m_screenAudio = false;
    int m_screenVolume = 100;
    bool m_uiSounds = true;
    bool m_mirrorSelf = true;
    int m_perPage = 9;
    bool m_showSelf = true;
    bool m_hideNoVideo = false;
    QString m_keyMic, m_keySound, m_keyCam;
    QString m_keyShare, m_keyFull, m_keyLeave;
    bool m_pushToTalk = false;
    bool m_devMode = false;

    qreal m_micLevel = 0;
    qint64 m_micLevelAt = 0;              // прореживание micLevelChanged
    int m_agcSensitivity = 100;           // проценты; в QSettings не попадает
};
