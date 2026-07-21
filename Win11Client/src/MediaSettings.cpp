#include "MediaSettings.h"
#include <QMediaDevices>
#include <QSettings>
#include <QDateTime>
#include <QKeySequence>

// Идентификатор устройства в настройках: id() устройства — бинарный QByteArray,
// в QSettings и QML он ездит как base64-строка.
static QString devId(const QByteArray& id) { return QString::fromLatin1(id.toBase64()); }

static const char* kGroup = "av";

MediaSettings::MediaSettings(QObject* parent) : QObject(parent) {
    QSettings s;
    s.beginGroup(kGroup);
    m_micId = s.value("micId").toString();
    m_camId = s.value("camId").toString();
    m_outId = s.value("outId").toString();
    m_volume = qBound(0, s.value("volume", 100).toInt(), 200);
    m_sensitivity = qBound(0, s.value("sens", 100).toInt(), 200);
    const QStringList levels{"low", "med", "high"};
    m_camQuality = s.value("qCam", "med").toString();
    if (!levels.contains(m_camQuality)) m_camQuality = "med";
    m_audioQuality = s.value("qAudio", "med").toString();
    if (!levels.contains(m_audioQuality)) m_audioQuality = "med";
    const QStringList resList{"360", "480", "720", "1080", "src"};
    m_screenRes = s.value("screenRes", "720").toString();
    if (!resList.contains(m_screenRes)) m_screenRes = "720";
    m_screenFps = s.value("screenFps", 30).toInt();
    if (m_screenFps != 15 && m_screenFps != 30 && m_screenFps != 60) m_screenFps = 30;
    m_keyMic = s.value("keyMic").toString();
    m_keySound = s.value("keySound").toString();
    m_keyCam = s.value("keyCam").toString();
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
void MediaSettings::setKeyMic(const QString& s) {
    if (m_keyMic == s) return;
    m_keyMic = s;
    save("keyMic", s);
    emit keyMicChanged();
}
void MediaSettings::setKeySound(const QString& s) {
    if (m_keySound == s) return;
    m_keySound = s;
    save("keySound", s);
    emit keySoundChanged();
}
void MediaSettings::setKeyCam(const QString& s) {
    if (m_keyCam == s) return;
    m_keyCam = s;
    save("keyCam", s);
    emit keyCamChanged();
}

QString MediaSettings::sequenceFromEvent(int key, int modifiers) const {
    switch (key) {
    case Qt::Key_Control: case Qt::Key_Shift: case Qt::Key_Alt:
    case Qt::Key_Meta:    case Qt::Key_AltGr: case Qt::Key_unknown:
    case Qt::Key_Escape:  case Qt::Key_F11:      // Esc/F11 держат полный экран
        return QString();                         // одинокий модификатор — не бинд
    default: break;
    }
    // Модификатор не обязателен — можно назначить одну клавишу. Пока окно в
    // фокусе, бинды глушатся при вводе в чат, а глобально пользователь сам
    // выбирает свободную клавишу (нумпад, F-клавиша…).
    return QKeySequence(key | modifiers).toString(QKeySequence::PortableText);
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
    return { w, h, m_screenFps, int(base * k) };
}

int MediaSettings::audioBitrate() const {
    if (m_audioQuality == "low")  return 16000;
    if (m_audioQuality == "high") return 64000;
    return 32000;
}

// ---------- индикатор микрофона ----------

void MediaSettings::reportMicLevel(qreal level) {
    m_micLevel = level;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_micLevelAt < 100 && level > 0) return;   // ~10 Гц достаточно
    m_micLevelAt = now;
    emit micLevelChanged();
}
