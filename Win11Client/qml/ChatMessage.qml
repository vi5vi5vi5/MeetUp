import QtQuick
import MeetUp

// One chat entry. `self` right-aligns and fills the bubble with accent;
// others are inset surface bubbles on the left with an author line above.
Item {
    id: root

    property string author: ""
    property string text: ""
    property string time: ""
    property bool self: false

    readonly property real _maxContent: width * 0.82 - 24

    implicitHeight: col.implicitHeight
    // width supplied by the containing list

    Column {
        id: col
        width: parent.width
        spacing: 3

        Row {
            visible: !root.self && (root.author !== "" || root.time !== "")
            leftPadding: 4
            spacing: 6
            Text {
                text: root.author
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.text2xs
                font.weight: Font.DemiBold
            }
            Text {
                visible: root.time !== ""
                text: root.time
                color: Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.text2xs
            }
        }

        Rectangle {
            id: bubble
            x: root.self ? parent.width - width : 0
            width: Math.min(msg.implicitWidth, root._maxContent) + 24
            height: msg.implicitHeight + 16
            color: root.self ? Theme.accent : Theme.surface2
            radius: Theme.radiusSm
            bottomRightRadius: root.self ? 3 : Theme.radiusSm
            bottomLeftRadius: root.self ? Theme.radiusSm : 3

            Text {
                id: msg
                x: 12; y: 8
                width: Math.min(implicitWidth, root._maxContent)
                text: root.text
                wrapMode: Text.Wrap
                color: root.self ? Theme.accentFg : Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
            }
        }
    }
}
