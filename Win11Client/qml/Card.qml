import QtQuick
import QtQuick.Effects
import MeetUp

// Surface panel: rounded (16), hairline border, soft shadow when `elevated`.
// Children stack vertically in an inner column (set `spacing`); give the card
// a width and its height follows the content.
Item {
    id: root

    property bool elevated: false
    property int padding: Theme.padCard
    property int spacing: 14
    default property alias content: holder.data

    // Естественная ширина — по самому широкому ребёнку, а НЕ по holder: дети
    // растянуты по нему (width: parent.width), holder растянут по карточке, и
    // implicitWidth: holder.implicitWidth сводился к тождеству
    // implicitWidth == width. Раскладка из такой «подсказки» ничего не узнаёт,
    // а замкнутая на собственный размер величина — готовый binding loop.
    implicitWidth: {
        var w = 0
        for (var i = 0; i < holder.children.length; ++i) {
            var c = holder.children[i]
            if (c.visible) w = Math.max(w, c.implicitWidth)
        }
        return w + padding * 2
    }
    implicitHeight: holder.implicitHeight + padding * 2

    MultiEffect {
        source: surface
        anchors.fill: surface
        visible: root.elevated
        z: -1
        shadowEnabled: true
        shadowColor: Theme.shadowColor
        shadowVerticalOffset: 12
        shadowHorizontalOffset: 0
        shadowBlur: 0.9
        autoPaddingEnabled: true
    }

    Rectangle {
        id: surface
        anchors.fill: parent
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }

    Column {
        id: holder
        x: root.padding
        y: root.padding
        width: root.width - root.padding * 2
        spacing: root.spacing
    }
}
