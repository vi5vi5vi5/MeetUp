import QtQuick
import MeetUp

// Square icon button. variant: neutral · accent · active (lime-tinted "on") ·
// off (muted, for disabled mic/cam) · danger. size: sm 40 / md 52 / lg 60.
// `wide` stretches width 1.5x (hang-up button).
Item {
    id: root

    property string icon: ""
    property string variant: "neutral"
    property string size: "md"
    property bool wide: false
    signal clicked()

    readonly property int _s: size === "sm" ? 40 : size === "lg" ? 60 : 52
    readonly property int _iconSize: size === "sm" ? 18 : size === "lg" ? 26 : 22

    implicitWidth: wide ? Math.round(_s * 1.5) : _s
    implicitHeight: _s

    readonly property color _bg: {
        switch (variant) {
        case "accent": return Theme.accent
        case "danger": return Theme.danger
        case "active": return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
        case "off":    return Theme.surface2
        default:       return Theme.surface
        }
    }
    readonly property color _fg: {
        switch (variant) {
        case "accent": return Theme.accentFg
        case "danger": return Theme.dangerFg
        case "active": return Theme.accentInk
        case "off":    return Theme.textMuted
        default:       return Theme.text
        }
    }
    readonly property color _border: {
        switch (variant) {
        case "active": return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45)
        case "neutral":
        case "off":    return Theme.border
        default:       return "transparent"
        }
    }

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: Theme.radiusMd
        color: ma.pressed ? Qt.darker(root._bg, 1.12)
             : ma.containsMouse ? Qt.lighter(root._bg, root.variant === "neutral" || root.variant === "off" ? 1.35 : 1.06)
             : root._bg
        border.width: 1
        border.color: root._border
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    AppIcon {
        anchors.centerIn: parent
        name: root.icon
        size: root._iconSize
        color: root._fg
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
