import QtQuick
import MeetUp

// Pill badge. tone: muted · live · accent · danger. `dot` adds a leading
// status dot; `solid` fills the pill with the tone colour.
Item {
    id: root

    property string text: ""
    property string tone: "muted"
    property bool dot: false
    property bool solid: false

    readonly property color _toneColor: {
        switch (tone) {
        case "live":   return Theme.live
        case "accent": return Theme.accentInk
        case "danger": return Theme.danger
        default:       return Theme.textMuted
        }
    }
    readonly property color _fg: solid ? Theme.accentFg : (tone === "muted" ? Theme.textMuted : _toneColor)

    implicitWidth: row.implicitWidth + 22
    implicitHeight: label.implicitHeight + 12

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPill
        color: root.solid ? root._toneColor : Theme.surface2
        border.width: root.solid ? 0 : 1
        border.color: Theme.border
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            visible: root.dot
            anchors.verticalCenter: parent.verticalCenter
            width: 7; height: 7; radius: 3.5
            color: root.solid ? Theme.accentFg : root._toneColor
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root._fg
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
            font.weight: Font.DemiBold
        }
    }
}
