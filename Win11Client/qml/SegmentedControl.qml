import QtQuick
import MeetUp

// Выбор одного из нескольких равнозначных вариантов, когда их два-четыре и
// они коротко называются (тема, кодек, размер сетки). Выпадающий список для
// такого — лишний клик: здесь все варианты видны сразу.
// Модель — [{id, label, soon}]; soon помечает вариант, который ещё не работает
// (например «как в системе» у темы) — он виден, но не нажимается.
Item {
    id: root

    property var model: []
    property string current: ""
    signal picked(string id)

    width: parent ? parent.width : 0
    implicitHeight: 38
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm
        color: Theme.surface2
        border.width: 1
        border.color: Theme.border
    }

    Row {
        id: row
        anchors.fill: parent
        anchors.margins: 3
        spacing: 3

        Repeater {
            model: root.model
            delegate: Rectangle {
                id: seg
                required property var modelData

                readonly property bool isCurrent: root.current === modelData.id
                readonly property bool isSoon: modelData.soon === true

                width: (row.width - (root.model.length - 1) * row.spacing) / root.model.length
                height: row.height
                radius: Theme.radiusXs
                color: seg.isCurrent ? Theme.accentSoft : "transparent"
                border.width: seg.isCurrent ? 1 : 0
                border.color: Theme.accentLine
                opacity: seg.isSoon ? 0.5 : 1
                Behavior on color { ColorAnimation { duration: Theme.durFast } }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 12
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: seg.modelData.label
                    color: seg.isCurrent ? Theme.accentInk : Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                    font.weight: Font.DemiBold
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    enabled: !seg.isSoon
                    onTapped: root.picked(seg.modelData.id)
                }
                HoverHandler {
                    enabled: !seg.isSoon
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }
}
