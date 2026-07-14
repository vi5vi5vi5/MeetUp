import QtQuick
import MeetUp

// Text button. variant: primary (lime) · secondary (surface+border) ·
// ghost (border only) · danger (red). size: sm | md | lg.
Item {
    id: root

    property string text: ""
    property string variant: "primary"
    property string size: "md"
    property string icon: ""
    property string iconRight: ""
    signal clicked()

    readonly property int _padV: size === "sm" ? 8 : size === "lg" ? 14 : 11
    readonly property int _padH: size === "sm" ? 12 : size === "lg" ? 22 : 18
    readonly property int _fontSize: size === "sm" ? 12 : size === "lg" ? 16 : 14
    readonly property int _iconSize: size === "sm" ? 14 : size === "lg" ? 18 : 16

    readonly property color _bg: variant === "primary" ? Theme.accent
                               : variant === "danger" ? Theme.danger
                               : variant === "secondary" ? Theme.surface
                               : "transparent"
    readonly property color _fg: variant === "primary" ? Theme.accentFg
                               : variant === "danger" ? Theme.dangerFg
                               : Theme.text
    readonly property bool _outlined: variant === "secondary" || variant === "ghost"

    implicitWidth: row.implicitWidth + _padH * 2
    implicitHeight: row.implicitHeight + _padV * 2
    opacity: enabled ? 1 : 0.45

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: Theme.radiusMd
        color: {
            if (!root.enabled) return root._bg
            if (ma.pressed) return Qt.darker(root._bg === "transparent" ? Theme.surface2 : root._bg, 1.12)
            if (ma.containsMouse && root.variant === "ghost") return Theme.surface2
            if (ma.containsMouse && root.variant === "secondary") return Theme.surface3
            if (ma.containsMouse && (root.variant === "primary" || root.variant === "danger"))
                return Qt.lighter(root._bg, 1.06)
            return root._bg
        }
        border.width: root._outlined ? 1 : 0
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8

        AppIcon {
            visible: root.icon !== ""
            anchors.verticalCenter: parent.verticalCenter
            name: root.icon
            size: root._iconSize
            color: root._fg
        }
        Text {
            visible: root.text !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root._fg
            font.family: Theme.uiFont
            font.pixelSize: root._fontSize
            font.weight: Font.Bold
        }
        AppIcon {
            visible: root.iconRight !== ""
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconRight
            size: root._iconSize
            color: root._fg
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
