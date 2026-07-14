import QtQuick
import MeetUp

// Round avatar showing the user's initials (photo support comes with M6).
Item {
    id: root
    property string name: ""
    property real size: 40
    property color bg: Theme.surface3

    implicitWidth: size
    implicitHeight: size

    readonly property string _initials: {
        var p = String(name).trim().split(/\s+/).filter(function (s) { return s.length > 0 })
        if (!p.length) return "?"
        if (p.length === 1) return p[0].substring(0, 2).toUpperCase()
        return (p[0][0] + p[1][0]).toUpperCase()
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.bg
        border.width: 1
        border.color: Theme.border
        Text {
            anchors.centerIn: parent
            text: root._initials
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Math.round(root.size * 0.4)
            font.weight: Font.ExtraBold
        }
    }
}
