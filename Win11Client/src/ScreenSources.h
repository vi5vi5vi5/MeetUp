#pragma once
#include <QObject>
#include <QHash>
#include <QImage>
#include <QString>
#include <QVariantList>
#include <QCapturableWindow>
#include <QQuickImageProvider>

class QScreen;

// Источники демонстрации экрана (M7). В браузере объект захвата выбирает сам
// браузер своим системным диалогом; у нас такого диалога нет — значит выбор
// рисуем сами, и ему нужны и список, и превью. Этот класс перечисляет мониторы
// и окна, снимает миниатюры мониторов и помнит выбранное.
// Из QML виден как Screens.
class ScreenSources : public QObject {
    Q_OBJECT
    // [{ key, label, sub, thumb }] — thumb пуст, если снимок не удался.
    Q_PROPERTY(QVariantList screens READ screens NOTIFY listsChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY listsChanged)
    // Ключ выбранного источника ("screen:0" / "window:3") и его подпись —
    // подпись показывается на плашке «вы демонстрируете…».
    Q_PROPERTY(QString selectedKey   READ selectedKey   NOTIFY selectedChanged)
    Q_PROPERTY(QString selectedLabel READ selectedLabel NOTIFY selectedChanged)
public:
    explicit ScreenSources(QObject* parent = nullptr);

    QVariantList screens() const { return m_screens; }
    QVariantList windows() const { return m_windows; }
    QString selectedKey() const { return m_selKey; }
    QString selectedLabel() const { return m_selLabel; }

    // Пересобрать списки и переснять миниатюры мониторов. Зовётся при открытии
    // модалки выбора: окна открываются и закрываются, кадр монитора устаревает.
    Q_INVOKABLE void refresh();
    // Доснять миниатюры окон. Вынесено отдельно намеренно: снимок монитора
    // стоит ~70 мс, а вот PrintWindow каждого окна — ~30 мс, и на десятке окон
    // это заметная пауза. Платим за неё, только когда открыли вкладку «Окно».
    Q_INVOKABLE void refreshWindowThumbs();
    // Выбрать источник по ключу. Пустой ключ — сбросить выбор.
    Q_INVOKABLE void select(const QString& key);

    // ---- для VideoEngine (не QML) ----
    bool hasSelection() const { return !m_selKey.isEmpty(); }
    bool isWindow() const { return m_selKey.startsWith("window:"); }
    QScreen* selectedScreen() const;            // nullptr, если выбрано окно
    QCapturableWindow selectedWindow() const;   // невалидное, если выбран монитор

    // Кэш миниатюр для image-провайдера (ключ — тот же key источника).
    QImage thumb(const QString& key) const { return m_thumbs.value(key); }

signals:
    void listsChanged();
    void selectedChanged();

private:
    void rebuildScreens();
    void rebuildWindows();
    QString thumbUrl(const QString& key) const;   // "" — миниатюры нет

    QVariantList m_screens, m_windows;
    QList<QCapturableWindow> m_winHandles;   // параллельно m_windows
    QHash<QString, QImage> m_thumbs;
    QString m_selKey, m_selLabel;
    int m_nonce = 0;                         // ломает кэш QML-картинок
};

// Отдаёт QML миниатюры из кэша ScreenSources. Сами снимки делает ScreenSources
// на GUI-потоке (QScreen::grabWindow с чужого потока на Windows — лотерея),
// провайдер только выдаёт готовое.
class ScreenThumbProvider : public QQuickImageProvider {
public:
    explicit ScreenThumbProvider(ScreenSources* src)
        : QQuickImageProvider(QQuickImageProvider::Image), m_src(src) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;
private:
    ScreenSources* m_src;   // не владеем: живёт дольше движка QML
};
