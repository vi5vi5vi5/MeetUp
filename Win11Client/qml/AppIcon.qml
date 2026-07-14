import QtQuick
import QtQuick.Effects

// A Lucide line-icon (2px stroke) rendered from an SVG asset and tinted to
// `color` via colorization. `name` is the file stem under resources/icons/.
Item {
    id: root

    property string name: ""
    property color color: "#ffffff"
    property real size: 20

    implicitWidth: size
    implicitHeight: size

    Image {
        id: img
        anchors.fill: parent
        source: root.name ? Qt.resolvedUrl("../resources/icons/" + root.name + ".svg") : ""
        sourceSize.width: root.size * 2
        sourceSize.height: root.size * 2
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: img
        colorization: 1.0
        colorizationColor: root.color
    }
}
