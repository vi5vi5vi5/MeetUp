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
