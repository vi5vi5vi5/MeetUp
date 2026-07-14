import QtQuick
import QtQuick.Layouts
import MeetUp

// Inset row showing a room invite link with a copy button (reproduces the web
// .hu-link). Copy shows a brief check; real clipboard write is wired later.
Rectangle {
    id: root
    property string code: ""
    readonly property string linkText: MockData.serverAddress + "/conference.html?room=" + code

    implicitHeight: 46
    radius: Theme.radiusMd
    color: Theme.surface2
    border.width: 1
    border.color: Theme.border

    property bool _copied: false

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: root.linkText
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.monoFont
            font.pixelSize: 12
        }
        IconButton {
            size: "sm"
            icon: root._copied ? "check" : "copy"
            variant: root._copied ? "active" : "neutral"
            onClicked: { root._copied = true; copyReset.restart() }
        }
    }
    Timer { id: copyReset; interval: 1400; onTriggered: root._copied = false }
}
