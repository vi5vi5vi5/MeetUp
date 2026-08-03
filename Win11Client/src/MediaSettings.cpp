#include "MediaSettings.h"
#include "HotkeySpec.h"
#include <QMediaDevices>
#include <QSettings>
#include <QDateTime>
#include <QtMath>

// Идентификатор устройства в настройках: id() устройства — бинарный QByteArray,
// в QSettings и QML он ездит как base64-строка.
static QString devId(const QByteArray& id) { return QString::fromLatin1(id.toBase64()); }

static const char* kGroup = "av";

// Потолок битрейта демонстрации: "auto" либо кбит/с строкой. Список закрытый —
// значение приходит из выпадающего списка, а из чужого файла настроек может
// прийти что угодно.
static bool knownScreenBitrate(const QString& b) {
    static const QStringList kAll{ "auto", "500", "1000", "2000", "5000",
                                   "10000", "20000", "40000", "80000" };
    return kAll.contains(b);
}

MediaSettings::MediaSettings(QObject* parent) : QObject(parent) {
    QSettings s;
    s.beginGroup(kGroup);
    m_micId = s.value("micId").toString();
    m_camId = s.value("camId").toString();
    m_outId = s.value("outId").toString();
    m_volume = qBound(0, s.value("volume", 100).toInt(), 200);
    m_sensitivity = qBound(0, s.value("sens", 100).toInt(), 200);
    m_noiseSuppression = s.value("noiseSuppression", true).toBool();
    m_autoGain = s.value("autoGain", true).toBool();
    // Ползунок при включённом автоусилении показывает не сохранённое число, а
    // то, что насчитает AutoGain на живом звуке. До первого кадра показать
    // нечего — берём ручное значение, с него автоусиление и стартует.
    m_agcSensitivity = m_sensitivity;
    const QStringList levels{"low", "med", "high"};
    m_camQuality = s.value("qCam", "med").toString();
    if (!levels.contains(m_camQuality)) m_camQuality = "med";
    m_audioQuality = s.value("qAudio", "med").toString();
    if (!levels.contains(m_audioQuality)) m_audioQuality = "med";
    const QStringList resList{"360", "480", "720", "1080", "src"};
    m_screenRes = s.value("screenRes", "720").toString();
    if (!resList.contains(m_screenRes)) m_screenRes = "720";
    // ЗАМЕТКА НА БУДУЩЕЕ: частота «Источник», по образцу разрешения. Свой
    // захват (ScreenCapturer) отдаёт кадры с частотой обновления монитора, а не
    // по таймеру, — значит на мониторе 144 или 240 Гц он честно даст столько же.
    // Само по себе это красиво, но включать такое вслепую нельзя: битрейт уйдёт
    // на уровень облачного гейминга, а приёмник (особенно веб) столько кадров в
    // секунду не разберёт. Делать вместе с этим: потолок частоты по возможностям
    // приёмников и предупреждение в интерфейсе, что это режим «для своих».
    // Пункт из разряда «когда всё остальное готово».
    m_screenFps = s.value("screenFps", 30).toInt();
    if (m_screenFps != 15 && m_screenFps != 30 && m_screenFps != 60) m_screenFps = 30;
    m_screenBitrate = s.value("screenBitrate", "auto").toString();
    if (!knownScreenBitrate(m_screenBitrate)) m_screenBitrate = "auto";
    m_screenCursor = s.value("screenCursor", true).toBool();
    // Кодеки. Значение — имя кодировщика FFmpeg; проверять его здесь не надо,
    // незнакомое просто не найдётся в каталоге и сработает как «Авто». Ключи
    // новые (codecScreen/codecCam), а не прежний screenCodec: у того значения
    // были другого рода («auto», «hevc», «av1»), и прочитать их как имена
    // кодировщиков значило бы тихо подсунуть человеку не тот выбор.
    m_screenCodec = s.value("codecScreen", "auto").toString();
    m_camCodec = s.value("codecCam", "auto").toString();
    m_rxBuffer = s.value("rxBuffer", true).toBool();
    m_txBuffer = s.value("txBuffer", true).toBool();
    m_screenAudio = s.value("screenAudio", false).toBool();
    m_screenVolume = qBound(0, s.value("screenVolume", 100).toInt(), 200);
    m_uiSounds = s.value("uiSounds", true).toBool();
    // Через normalize — в файле может лежать текст QKeySequence от прежней
    // версии ("Ctrl+D", "Num+5"); что переносится, то переносится, остальное
    // молча очищается и назначается заново.
    m_keyMic = HotkeySpec::normalize(s.value("keyMic").toString());
    m_keySound = HotkeySpec::normalize(s.value("keySound").toString());
    m_keyCam = HotkeySpec::normalize(s.value("keyCam").toString());
    s.endGroup();

    // Горячее подключение/отключение устройств: списки в модалке живые.
    m_devices = new QMediaDevices(this);
    connect(m_devices, &QMediaDevices::audioInputsChanged, this, &MediaSettings::devicesChanged);
    connect(m_devices, &QMediaDevices::audioOutputsChanged, this, &MediaSettings::devicesChanged);
    connect(m_devices, &QMediaDevices::videoInputsChanged, this, &MediaSettings::devicesChanged);
}

void MediaSettings::save(const QString& key, const QVariant& value) {
    QSettings s;
    s.beginGroup(kGroup);
    s.setValue(key, value);
    s.endGroup();
}

// ---------- списки устройств ----------

template <typename Dev>
static QVariantList listDevices(const QList<Dev>& devs, const QString& defaultLabel) {
    QVariantList out;
    QVariantMap def;
    def["id"] = "";
    def["label"] = defaultLabel;
    out.append(def);
    for (const Dev& d : devs) {
        QVariantMap m;
        m["id"] = devId(d.id());
        m["label"] = d.description();
        out.append(m);
    }
    return out;
}

QVariantList MediaSettings::micDevices() const {
    return listDevices(QMediaDevices::audioInputs(), QStringLiteral("Системный по умолчанию"));
}
QVariantList MediaSettings::camDevices() const {
    return listDevices(QMediaDevices::videoInputs(), QStringLiteral("Системная по умолчанию"));
}
QVariantList MediaSettings::outDevices() const {
    return listDevices(QMediaDevices::audioOutputs(), QStringLiteral("Системные по умолчанию"));
}

// ---------- сеттеры ----------

void MediaSettings::setMicId(const QString& id) {
    if (m_micId == id) return;
    m_micId = id;
    save("micId", id);
    emit micIdChanged();
}
void MediaSettings::setCamId(const QString& id) {
    if (m_camId == id) return;
    m_camId = id;
    save("camId", id);
    emit camIdChanged();
}
void MediaSettings::setOutId(const QString& id) {
    if (m_outId == id) return;
    m_outId = id;
    save("outId", id);
    emit outIdChanged();
}
void MediaSettings::setVolume(int v) {
    v = qBound(0, v, 200);
    if (m_volume == v) return;
    m_volume = v;
    save("volume", v);
    emit volumeChanged();
}
void MediaSettings::setSensitivity(int v) {
    v = qBound(0, v, 200);
    if (m_sensitivity == v) return;
    m_sensitivity = v;
    save("sens", v);
    emit sensitivityChanged();
}
void MediaSettings::setCamQuality(const QString& q) {
    if (m_camQuality == q || (q != "low" && q != "med" && q != "high")) return;
    m_camQuality = q;
    save("qCam", q);
    emit camQualityChanged();
}
void MediaSettings::setScreenRes(const QString& r) {
    if (m_screenRes == r) return;
    if (r != "360" && r != "480" && r != "720" && r != "1080" && r != "src") return;
    m_screenRes = r;
    save("screenRes", r);
    emit screenResChanged();
}
void MediaSettings::setScreenFps(int fps) {
    if (m_screenFps == fps) return;
    if (fps != 15 && fps != 30 && fps != 60) return;
    m_screenFps = fps;
    save("screenFps", fps);
    emit screenFpsChanged();
}

void MediaSettings::setScreenBitrate(const QString& b) {
    if (m_screenBitrate == b || !knownScreenBitrate(b)) return;
    m_screenBitrate = b;
    save("screenBitrate", b);
    emit screenBitrateChanged();
}

void MediaSettings::setScreenCodec(const QString& id) {
    if (m_screenCodec == id) return;
    m_screenCodec = id;
    save("codecScreen", id);
    emit screenCodecChanged();
}

void MediaSettings::setCamCodec(const QString& id) {
    if (m_camCodec == id) return;
    m_camCodec = id;
    save("codecCam", id);
    emit camCodecChanged();
}

void MediaSettings::setRxBuffer(bool on) {
    if (m_rxBuffer == on) return;
    m_rxBuffer = on;
    save("rxBuffer", on);
    emit rxBufferChanged();
}

void MediaSettings::setTxBuffer(bool on) {
    if (m_txBuffer == on) return;
    m_txBuffer = on;
    save("txBuffer", on);
    emit txBufferChanged();
}

void MediaSettings::setScreenCursor(bool on) {
    if (m_screenCursor == on) return;
    m_screenCursor = on;
    save("screenCursor", on);
    emit screenCursorChanged();
}

void MediaSettings::setNoiseSuppression(bool on) {
    if (m_noiseSuppression == on) return;
    m_noiseSuppression = on;
    save("noiseSuppression", on);
    emit noiseSuppressionChanged();
    // Автоусиление гаснет и загорается вместе с шумоподавлением (см.
    // autoGain()). Сообщить об этом надо, только если человек его выбирал:
    // иначе ничего и не изменилось.
    if (m_autoGain) {
        resetAgcReadout();
        emit autoGainChanged();
    }
}

void MediaSettings::setAutoGain(bool on) {
    if (m_autoGain == on) return;
    m_autoGain = on;
    save("autoGain", on);
    resetAgcReadout();
    emit autoGainChanged();
}

void MediaSettings::resetAgcReadout() {
    if (autoGain() || m_agcSensitivity == m_sensitivity) return;
    m_agcSensitivity = m_sensitivity;
    emit agcSensitivityChanged();
}

void MediaSettings::setUiSounds(bool on) {
    if (m_uiSounds == on) return;
    m_uiSounds = on;
    save("uiSounds", on);
    emit uiSoundsChanged();
}

void MediaSettings::setScreenAudio(bool on) {
    if (m_screenAudio == on) return;
    m_screenAudio = on;
    save("screenAudio", on);
    emit screenAudioChanged();
}

void MediaSettings::setScreenVolume(int v) {
    v = qBound(0, v, 200);
    if (m_screenVolume == v) return;
    m_screenVolume = v;
    save("screenVolume", v);
    emit screenVolumeChanged();
}
// Бинды приводим к каноническому виду на входе, а не при чтении: так в файле
// настроек и в интерфейсе всегда одно и то же написание, откуда бы значение ни
// пришло (см. HotkeySpec).
void MediaSettings::setKeyMic(const QString& s) {
    const QString v = HotkeySpec::normalize(s);
    if (m_keyMic == v) return;
    m_keyMic = v;
    save("keyMic", v);
    emit keyMicChanged();
}
void MediaSettings::setKeySound(const QString& s) {
    const QString v = HotkeySpec::normalize(s);
    if (m_keySound == v) return;
    m_keySound = v;
    save("keySound", v);
    emit keySoundChanged();
}
void MediaSettings::setKeyCam(const QString& s) {
    const QString v = HotkeySpec::normalize(s);
    if (m_keyCam == v) return;
    m_keyCam = v;
    save("keyCam", v);
    emit keyCamChanged();
}

void MediaSettings::setAudioQuality(const QString& q) {
    if (m_audioQuality == q || (q != "low" && q != "med" && q != "high")) return;
    m_audioQuality = q;
    save("qAudio", q);
    emit audioQualityChanged();
}

// ---------- разрешение устройств ----------

QAudioDevice MediaSettings::audioInput() const {
    for (const QAudioDevice& d : QMediaDevices::audioInputs())
        if (devId(d.id()) == m_micId) return d;
    return QMediaDevices::defaultAudioInput();
}
QAudioDevice MediaSettings::audioOutput() const {
    for (const QAudioDevice& d : QMediaDevices::audioOutputs())
        if (devId(d.id()) == m_outId) return d;
    return QMediaDevices::defaultAudioOutput();
}
QCameraDevice MediaSettings::camera() const {
    for (const QCameraDevice& d : QMediaDevices::videoInputs())
        if (devId(d.id()) == m_camId) return d;
    return QMediaDevices::defaultVideoInput();
}

// ---------- пресеты (методичка §5.5, как у веба) ----------

MediaSettings::CamPreset MediaSettings::camPreset() const {
    if (m_camQuality == "low")  return { 640, 360, 15, 400000 };
    if (m_camQuality == "high") return { 1920, 1080, 30, 2500000 };
    return { 1280, 720, 24, 1200000 };
}

// Экран. Ширина взята под 16:9, но она — только потолок: кадр вписывается в
// рамку по меньшей стороне, поэтому у монитора 16:10 «720p» даст 1152×720.
// «Источник» = рамка заведомо больше любого экрана, масштабирования нет.
MediaSettings::CamPreset MediaSettings::screenPreset() const {
    int w = 1280, h = 720, base = 1800000;
    if (m_screenRes == "360")       { w = 640;  h = 360;  base = 500000; }
    else if (m_screenRes == "480")  { w = 854;  h = 480;  base = 900000; }
    else if (m_screenRes == "1080") { w = 1920; h = 1080; base = 3500000; }
    else if (m_screenRes == "src")  { w = 7680; h = 4320; base = 6000000; }

    // Битрейт от частоты: вдвое больше кадров — далеко не вдвое больше данных
    // (на экране меняется малая часть картинки), поэтому коэффициенты пологие.
    const double k = m_screenFps <= 15 ? 0.7 : (m_screenFps >= 60 ? 1.6 : 1.0);
    int bitrate = int(base * k);

    // Явно выбранный потолок отменяет весь расчёт выше. Смысл настройки именно
    // в этом: расчёт исходит из «канал дорог, экономим», а у того, кто поднял
    // сервер себе домой, канал как раз дешёвый — и разумнее отдать биты, чем
    // смотреть на замыленный текст. Регулятор качества всё равно опустится
    // ниже этого потолка, если канал вдруг не потянет.
    if (m_screenBitrate != "auto") {
        bool ok = false;
        const int kbps = m_screenBitrate.toInt(&ok);
        if (ok && kbps > 0) bitrate = kbps * 1000;
    }
    return { w, h, m_screenFps, bitrate };
}

int MediaSettings::audioBitrate() const {
    if (m_audioQuality == "low")  return 16000;
    if (m_audioQuality == "high") return 64000;
    return 32000;
}

// ---------- индикатор микрофона ----------

void MediaSettings::reportMicLevel(qreal level) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_micLevelAt < 100 && level > 0) return;   // ~10 Гц достаточно
    // Значение меняем только вместе с уведомлением: присвоение до проверки
    // оставляло QML с новым числом, о котором ему никто не сказал, — полоска
    // замирала на старом делении до следующего разрешённого тика.
    m_micLevelAt = now;
    m_micLevel = level;
    emit micLevelChanged();
}

void MediaSettings::reportAgcGain(qreal gain) {
    // Округляем до шага ползунка: иначе ручка ходила бы на полделения туда-сюда
    // там, где автоусиление, по сути, стоит на месте.
    const int percent = qBound(0, int(qRound(gain * 100 / 5.0)) * 5, 10000);
    if (m_agcSensitivity == percent) return;
    m_agcSensitivity = percent;
    emit agcSensitivityChanged();
}
