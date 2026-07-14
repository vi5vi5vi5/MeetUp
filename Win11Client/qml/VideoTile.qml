import QtQuick
import MeetUp

// A participant tile. Monochrome gradient stands in for the (not-yet-wired)
// video; camera-off shows an initials avatar; a 2px accent border marks the
// active speaker; a frosted chip carries the name and muted-mic indicator.
Item {
    id: root

    property string name: ""
    property bool mic: true
    property bool cam: true
    property bool speaking: false
    property bool isSelf: false

    readonly property string _initials: {
        var parts = String(name).trim().split(/\s+/).filter(function (s) { return s.length > 0 })
        if (parts.length === 0) return "?"
        if (parts.length === 1) return parts[0].substring(0, 1).toUpperCase()
        return (parts[0].substring(0, 1) + parts[1].substring(0, 1)).toUpperCase()
    }

    Rectangle {
        id: tile
        anchors.fill: parent
        radius: Theme.radiusLg
        clip: true
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.isSelf ? Theme.tileSelfFrom : Theme.tileFrom }
            GradientStop { position: 1.0; color: root.isSelf ? Theme.tileSelfTo : Theme.tileTo }
        }
        border.width: root.speaking ? 2 : 1
        border.color: root.speaking ? Theme.accent : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

        // Faint watermark initials (stands in for a live video feed).
        Text {
            visible: root.cam
            anchors.centerIn: parent
            text: root._initials
            color: Qt.rgba(1, 1, 1, 0.10)
            font.family: Theme.displayFont
            font.pixelSize: Math.min(parent.width, parent.height) * 0.30
            font.weight: Font.Bold
        }

        // Avatar shown when the camera is off.
        Rectangle {
            visible: !root.cam
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height) * 0.34
            height: width
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.surface3 }
                GradientStop { position: 1.0; color: Theme.surface }
            }
            border.width: 1
            border.color: Theme.border
            Text {
                anchors.centerIn: parent
                text: root._initials
                color: Theme.text
                font.family: Theme.displayFont
                font.pixelSize: parent.width * 0.42
                font.weight: Font.DemiBold
            }
        }

        // Frosted name chip (bottom-left).
        Rectangle {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 10
            radius: Theme.radiusXs
            color: Theme.scrimChip
            height: chipRow.implicitHeight + 10
            width: chipRow.implicitWidth + 16

            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: 6
                AppIcon {
                    visible: !root.mic
                    anchors.verticalCenter: parent.verticalCenter
                    name: "mic-off"
                    size: 13
                    color: Theme.danger
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.name + (root.isSelf ? " · вы" : "")
                    color: Theme.dark ? "#ffffff" : Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                    font.weight: Font.Medium
                }
            }
        }
    }
}
