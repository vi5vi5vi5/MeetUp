#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>

class MediaSettings;

// Глобальные горячие клавиши (M8). QML-Shortcut работает только пока окно
// приложения в фокусе — а бинды нужны именно когда пользователь ушёл в другое
// окно (демонстрирует экран и хочет выключить микрофон, не возвращаясь). Здесь
// клавиши регистрируются в системе (RegisterHotKey), и Windows шлёт нам
// WM_HOTKEY независимо от фокуса.
//
// Глобальной клавише обязателен модификатор (Ctrl/Alt/Win) или роль
// функциональной: голая буква перехватывалась бы во всей системе. Сочетания
// берём из MediaSettings и перерегистрируем при их смене. Из QML — Hotkeys.
class GlobalHotkeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit GlobalHotkeys(MediaSettings* settings, QObject* parent = nullptr);
    ~GlobalHotkeys() override;

    // Слушаем клавиши, только пока активны: конференция открыта и не идёт
    // назначение клавиши (иначе бинд перехватывал бы сам себя при захвате).
    Q_INVOKABLE void setActive(bool active);

    bool nativeEventFilter(const QByteArray& type, void* message,
                           qintptr* result) override;

signals:
    void micHotkey();
    void soundHotkey();
    void camHotkey();
    // Сочетание занял другой процесс — регистрация не удалась. QML покажет.
    void conflict(const QString& sequence);

private:
    void reregister();                                  // снять всё и завести заново
    void unregisterAll();
    bool registerOne(int id, const QString& seqText);   // false — не вышло

    MediaSettings* m_settings;   // не владеем
    bool m_active = false;
    bool m_registered[3] = { false, false, false };
};
