import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import MeetUp

// Centered modal dialog over a dark scrim. Reproduces the web Modal: a titled,
// elevated card with a close button; clicking the scrim closes it. Child items
// stack in the body below the header.
Item {
    id: root

    property bool open: false
    property string title: ""
    property string subtitle: ""
    property real modalWidth: 460
    property int padding: Theme.padCard
    signal closed()

    default property alias content: body.data

    anchors.fill: parent
    visible: open
    z: 200

    // Scrim: tint + click-to-close.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(6 / 255, 6 / 255, 8 / 255, 0.55)
        MouseArea { anchors.fill: parent; onClicked: root.closed() }
    }

    MultiEffect {
        source: panel
        anchors.fill: panel
        shadowEnabled: true
        shadowColor: Theme.shadowColor
        shadowVerticalOffset: 12
        shadowBlur: 0.9
        autoPaddingEnabled: true
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(root.modalWidth, root.width - 40)
        // Заголовок фиксирован, тело прокручивается: панель не выше окна,
        // а содержимое, что не влезло, доступно скроллом (как .settings-card
        // { max-height; overflow:auto } у веба).
        height: Math.min(header.implicitHeight + bodyFlick.height
                         + col.spacing + root.padding * 2, root.height - 40)
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        // Absorb clicks so they don't reach the scrim behind.
        MouseArea { anchors.fill: parent }

        Column {
            id: col
            x: root.padding
            y: root.padding
            width: parent.width - root.padding * 2
            spacing: 14

            // Header: title/subtitle + close button (фиксирован, не скроллится).
            Item {
                id: header
                width: parent.width
                implicitHeight: Math.max(hcol.implicitHeight, 36)
                Column {
                    id: hcol
                    width: parent.width - 44
                    spacing: 4
                    Text {
                        visible: root.title !== ""
                        text: root.title
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: Theme.textXl
                        font.weight: Font.Bold
                        font.letterSpacing: -0.5
                    }
                    Text {
                        visible: root.subtitle !== ""
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: root.subtitle
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textSm
                    }
                }
                IconButton {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    size: "sm"
                    icon: "x"
                    onClicked: root.closed()
                }
            }

            // Body slot — прокручиваемый, если выше доступной высоты.
            Flickable {
                id: bodyFlick
                width: parent.width
                height: Math.min(body.implicitHeight,
                                 root.height - 40 - root.padding * 2
                                 - header.implicitHeight - col.spacing)
                contentWidth: width
                contentHeight: body.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    policy: bodyFlick.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }
                Column { id: body; width: bodyFlick.width; spacing: 14 }
            }
        }
    }
}
