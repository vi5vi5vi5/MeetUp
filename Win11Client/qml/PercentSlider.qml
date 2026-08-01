import QtQuick
import QtQuick.Controls.Basic
import MeetUp

// Слайдер процентов 0..200 (100 = «как есть») — громкость и чувствительность,
// один в один с вебом. Вынесен из SettingsModal: им пользуются два раздела.
Slider {
    id: slider
    from: 0
    to: 200
    stepSize: 5
    height: 26

    // Недоступный ползунок гасим ровно так же, как soon-строки: одинаковая
    // приглушённость по всему интерфейсу читается как «руками сюда нельзя».
    // Без этого он выглядит обычным и молча не отзывается на нажатие — а
    // «нажал и ничего» человек читает как поломку, а не как запрет.
    opacity: enabled ? 1 : 0.62

    // Ползунок бывает не только органом управления: при включённом
    // автоусилении чувствительность показывает то, что насчитал AutoGain, и
    // меняется сама примерно раз в 100 мс. Без сглаживания ручка дёргалась бы
    // рывками. Пока её тянут рукой, анимация выключена — иначе перетаскивание
    // становится резиновым.
    Behavior on value {
        enabled: !slider.pressed
        NumberAnimation { duration: 140; easing.type: Easing.OutQuad }
    }

    background: Rectangle {
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        width: slider.width
        height: 6
        radius: 3
        color: Theme.surface3
        Rectangle {
            width: slider.visualPosition * parent.width
            height: parent.height
            radius: 3
            color: Theme.accent
        }
    }
    handle: Rectangle {
        x: slider.visualPosition * (slider.width - width)
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        radius: 8
        color: Theme.text
        border.width: 1
        border.color: Theme.border
    }
}
