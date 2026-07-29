#pragma once
#include <QObject>
#include <QHash>
#include <QString>

class MediaSettings;
class AudioEngine;
class QSoundEffect;

// Звуки интерфейса. Виден из QML как Sfx.
//
// Десять коротких wav в resources/sounds — одно семейство, собранное из одного
// тембра: события различаются числом нот, интервалом и направлением, а не
// звучанием. Уровни сведены заранее, поэтому громкость здесь не крутится.
//
// Почему отдельный класс, а не SoundEffect прямо в QML. Решение «когда молчать»
// одно на все точки вызова, и разъехаться оно не должно: выключатель в
// настройках, тишина уведомлений при снятом звуке конференции, защита от пачек.
// Плюс QSoundEffect умеет играть в ВЫБРАННОЕ устройство вывода — уведомление
// обязано идти в те же наушники, что и разговор, а не в системные по умолчанию.
class UiSounds : public QObject {
    Q_OBJECT
public:
    UiSounds(MediaSettings* av, AudioEngine* audio, QObject* parent = nullptr);

    // Имя = имя файла без расширения: "toggle-on", "message", "peer-leave"…
    // Незнакомое имя молча игнорируется: опечатка в QML не повод падать, а
    // предупреждение о ненайденном файле всё равно придёт из build().
    Q_INVOKABLE void play(const QString& name);

private:
    void build();   // (пере)собрать пул под текущее устройство вывода

    MediaSettings* m_av;
    AudioEngine* m_audio;
    QHash<QString, QSoundEffect*> m_fx;
    QHash<QString, qint64> m_lastAt;   // имя -> когда играли (мс), против пачек
};
