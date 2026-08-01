import QtQuick
import MeetUp

// Строка-переключатель: название, поясняющая строчка мелким шрифтом и тумблер
// справа. Пояснение здесь не украшение — половина настроек непонятна без
// одной фразы о том, что именно изменится.
// soon: true — место занято, бэкенда нет: строка приглушается и не нажимается.
// Так же выглядит и строка, которой снаружи задали enabled: false, — настройка,
// недоступная из-за другой настройки, читается ровно как «пока нельзя».
Item {
    id: root

    property string label: ""
    property string description: ""
    property bool checked: false
    property bool soon: false
    signal toggled(bool value)

    width: parent ? parent.width : 0
    implicitHeight: Math.max(col.implicitHeight, sw.height)
    height: implicitHeight
    opacity: (soon || !enabled) ? 0.62 : 1
    enabled: !soon

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: sw.left
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Row {
            spacing: 8
            Text {
                text: root.label
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
                font.weight: Font.DemiBold
            }
            SoonChip {
                visible: root.soon
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Text {
            visible: root.description !== ""
            width: col.width
            wrapMode: Text.Wrap
            text: root.description
            color: Theme.textFaint
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
        }
    }

    AppToggle {
        id: sw
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        checked: root.checked
        onToggled: function (v) { root.toggled(v) }
    }
}
