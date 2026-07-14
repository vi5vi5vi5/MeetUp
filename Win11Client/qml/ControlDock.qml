import QtQuick
import QtQuick.Effects
import MeetUp

// Floating conference control bar: mic · camera · screen-share · settings ·
// divider · hang-up. Toggles are controlled from the outside.
Item {
    id: root

    property bool micOn: true
    property bool camOn: true
    property bool sharing: false

    signal toggleMic()
    signal toggleCam()
    signal toggleShare()
    signal openSettings()
    signal leave()

    implicitWidth: dockRow.implicitWidth + 20
    implicitHeight: dockRow.implicitHeight + 20

    MultiEffect {
        source: pill
        anchors.fill: pill
        z: -1
        shadowEnabled: true
        shadowColor: Theme.shadowColor
        shadowVerticalOffset: 8
        shadowBlur: 0.8
        autoPaddingEnabled: true
    }

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: Theme.radiusLg
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }

    Row {
        id: dockRow
        anchors.centerIn: parent
        spacing: Theme.gapInline

        IconButton {
            icon: root.micOn ? "mic" : "mic-off"
            variant: root.micOn ? "neutral" : "off"
            onClicked: root.toggleMic()
        }
        IconButton {
            icon: root.camOn ? "video" : "video-off"
            variant: root.camOn ? "neutral" : "off"
            onClicked: root.toggleCam()
        }
        IconButton {
            icon: "screen"
            variant: root.sharing ? "active" : "neutral"
            onClicked: root.toggleShare()
        }
        IconButton {
            icon: "settings"
            variant: "neutral"
            onClicked: root.openSettings()
        }
        Item {
            width: 1
            height: 52
            Rectangle {
                anchors.centerIn: parent
                width: 1; height: 36
                color: Theme.border
            }
        }
        IconButton {
            icon: "phone-off"
            variant: "danger"
            wide: true
            onClicked: root.leave()
        }
    }
}
