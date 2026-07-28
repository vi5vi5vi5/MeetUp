import QtQuick
import MeetUp

// Фирменный знак MeetUp. Скобки берут цвет темы, лаймовая точка в центре — нет.
// Поэтому две готовые заготовки, а не подкраска: MultiEffect с colorization
// (как в AppIcon) перекрасил бы и точку тоже — иконкам Lucide это можно, они
// одноцветные, знаку нельзя.
Item {
    id: root

    property real size: 26
    property bool plate: false          // тёмная плашка под знаком

    implicitWidth: size
    implicitHeight: size

    // Плашка — для непредсказуемого фона (обои, видео, чужая картинка).
    // Она всегда тёмная, в обеих темах: это тот же знак, что у .exe.
    Rectangle {
        anchors.fill: parent
        visible: root.plate
        radius: root.size * 0.22
        color: "#0e0e10"
    }

    Image {
        anchors.centerIn: parent
        width: root.plate ? Math.round(root.size * 0.8) : root.size
        height: width
        // На плашке знак всегда светлый, вне её — по теме.
        source: Qt.resolvedUrl(root.plate || Theme.dark
                               ? "../resources/brand/meetup-mark-light.svg"
                               : "../resources/brand/meetup-mark-dark.svg")
        sourceSize: Qt.size(width * 3, width * 3)   // запас на 225 % DPI
        smooth: true
        mipmap: true
    }
}
