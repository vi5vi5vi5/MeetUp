import QtQuick
import QtQuick.Layouts
import MeetUp

// Inset row showing a room invite link with a copy button (reproduces the web
// .hu-link). Показываем короткий вид (без https://), копируем полный URL.
Rectangle {
    id: root
    property string code: ""
    readonly property string linkText: Sys.host + "/conference.html?room=" + code

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
            onClicked: {
                Sys.copyText(Sys.roomLink(root.code))   // полная ссылка, с https://
                root._copied = true
                copyReset.restart()
            }
        }
    }
    Timer { id: copyReset; interval: 1400; onTriggered: root._copied = false }
}
