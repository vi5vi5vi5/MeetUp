#pragma once
#include <QObject>
#include <QVariantList>

// История посещённых комнат для главной — аналог meetup.history веба в
// localStorage, только в QSettings. Локальная, максимум 8 записей, сервер о
// ней не знает. Виден из QML как History.
class HistoryStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
public:
    explicit HistoryStore(QObject* parent = nullptr);

    QVariantList items() const { return m_items; }

    Q_INVOKABLE void record(const QString& code, const QString& title); // при входе в комнату
    Q_INVOKABLE void removeCode(const QString& code);                   // крестик у строки
    Q_INVOKABLE void clear();                                           // «Очистить»
    Q_INVOKABLE QString whenLabel(double atMs) const;                   // «12 мин назад»
    Q_INVOKABLE void refreshLabels() { emit itemsChanged(); }           // тик раз в минуту

signals:
    void itemsChanged();

private:
    void load();
    void save();
    QVariantList m_items;   // элементы: { code, title (опц.), at (мс эпохи) }
};
