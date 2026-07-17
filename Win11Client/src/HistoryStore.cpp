#include "HistoryStore.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QLocale>

static const char* kKey = "recentRooms";   // ключ в QSettings (HKCU\Software\MeetUp\MeetUp)

HistoryStore::HistoryStore(QObject* parent) : QObject(parent) {
    load();   // подхватываем историю прошлых запусков
}

void HistoryStore::load() {
    QSettings s;
    // Храним список одной JSON-строкой: переносимо и легко посмотреть глазами.
    const QJsonArray arr =
        QJsonDocument::fromJson(s.value(kKey).toString().toUtf8()).array();
    m_items.clear();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value("code").toString().isEmpty()) continue;   // битую запись — мимо
        m_items.append(o.toVariantMap());
    }
}

void HistoryStore::save() {
    QSettings s;
    if (m_items.isEmpty()) { s.remove(kKey); return; }
    QJsonArray arr;
    for (const QVariant& v : m_items)
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    s.setValue(kKey, QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void HistoryStore::record(const QString& code, const QString& title) {
    if (code.isEmpty()) return;
    // Дедупликация: старая запись этой комнаты уходит, новая встаёт наверх.
    for (int i = m_items.size() - 1; i >= 0; --i)
        if (m_items[i].toMap().value("code").toString() == code)
            m_items.removeAt(i);

    QVariantMap e;
    e["code"] = code;
    if (!title.isEmpty()) e["title"] = title;    // у разовых комнат заголовка нет
    // double: миллисекунды эпохи в int не влезают, а в QML всё равно поедет double.
    e["at"] = double(QDateTime::currentMSecsSinceEpoch());
    m_items.prepend(e);
    while (m_items.size() > 8) m_items.removeLast();   // как веб: slice(0, 8)

    save();
    emit itemsChanged();
}

void HistoryStore::removeCode(const QString& code) {
    for (int i = m_items.size() - 1; i >= 0; --i)
        if (m_items[i].toMap().value("code").toString() == code)
            m_items.removeAt(i);
    save();
    emit itemsChanged();
}

void HistoryStore::clear() {
    m_items.clear();
    save();
    emit itemsChanged();
}

// «только что» -> «N мин назад» -> «N ч назад» -> «17 июля» (fmtWhen веба).
QString HistoryStore::whenLabel(double atMs) const {
    const qint64 at = qint64(atMs);
    if (at <= 0) return {};
    const qint64 min = (QDateTime::currentMSecsSinceEpoch() - at) / 60000;
    if (min < 1)  return "только что";
    if (min < 60) return QString::number(min) + " мин назад";
    const qint64 h = min / 60;
    if (h < 24)   return QString::number(h) + " ч назад";
    return QLocale(QLocale::Russian)
        .toString(QDateTime::fromMSecsSinceEpoch(at).date(), "d MMMM");
}
