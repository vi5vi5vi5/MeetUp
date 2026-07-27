import QtQuick
import MeetUp

// Тумблер «вкл/выкл». Своё состояние не хранит как истину в последней
// инстанции: checked привязывается снаружи, наружу же уходит toggled —
// источник правды остаётся в AV/Theme, а не в интерфейсе.
Item {
    id: root

    property bool checked: false
    signal toggled(bool value)

    implicitWidth: 42
    implicitHeight: 24

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? Theme.accentSoft : Theme.surface3
        border.width: 1
        border.color: root.checked ? Theme.accentLine : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    Rectangle {
        id: knob
        width: 16
        height: 16
        radius: 8
        y: (parent.height - height) / 2
        x: root.checked ? parent.width - width - 4 : 4
        color: root.checked ? Theme.accentInk : Theme.textMuted
        Behavior on x { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    TapHandler {
        onTapped: {
            root.checked = !root.checked
            root.toggled(root.checked)
        }
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
