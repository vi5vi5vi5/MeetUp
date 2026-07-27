import QtQuick
import MeetUp

// Пилюля «скоро»: место в интерфейсе занято, а бэкенда за ним ещё нет.
// Сознательно тихая — приглушённая рамка без заливки: она сообщает о планах,
// а не борется за внимание с рабочими настройками.
Rectangle {
    id: root
    property string text: "скоро"

    implicitWidth: label.implicitWidth + 16
    implicitHeight: 18
    radius: Theme.radiusPill
    color: "transparent"
    border.width: 1
    border.color: Theme.borderStrong

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text.toUpperCase()
        color: Theme.textFaint
        font.family: Theme.labelFont
        font.pixelSize: 10
        font.letterSpacing: 1.2
        font.weight: Font.Medium
    }
}
