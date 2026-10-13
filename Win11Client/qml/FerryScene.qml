import QtQuick
import MeetUp

// Сцена в шапке карточки Ferry — по доскам «Виджет» и «Состояния» макета:
// паром у причала, в пути, у буя, в тумане, дома на закате. Только
// украшение (для экранного диктора её нет), поэтому анимация живёт, лишь
// пока идёт приём: в покое лента звонка не должна перерисовываться сама.
Rectangle {
    id: root

    // dock | seal | door | sail | wait | home | fog | seeds | empty
    property string mode: "dock"
    property real progress: 0          // для sail/wait — где паром на линии
    property bool animate: false

    implicitHeight: mode === "dock" || mode === "sail" || mode === "home" ? 84
                    : mode === "empty" ? 56 : 64
    clip: true

    gradient: Gradient {
        GradientStop { position: 0.0; color: root.mode === "fog" ? "#0d141a" : root.mode === "home" ? "#0b1015" : "#080c10" }
        GradientStop { position: 0.6; color: root.mode === "fog" ? "#131d25" : root.mode === "home" ? "#1d2932" : "#0f1820" }
        GradientStop { position: 1.0; color: root.mode === "fog" ? "#1a2630" : root.mode === "home" ? "#3d2b1f" : "#1a2833" }
    }

    function img(n) { return Qt.resolvedUrl("../resources/ferry/" + n + ".svg") }

    // ---- звёзды ----
    Repeater {
        model: root.mode === "fog" || root.mode === "home" ? []
               : root.mode === "dock" ? [[40, 14], [180, 24], [300, 10]]
               : root.mode === "sail" ? [[60, 12], [240, 20]]
               : root.mode === "wait" ? [[90, 10]]
               : root.mode === "empty" ? [[120, 10], [260, 14]]
               : [[36, 12], [230, 18]]
        delegate: Rectangle {
            required property var modelData
            required property int index
            x: modelData[0]; y: modelData[1]
            width: 2; height: 2; radius: 1
            color: FerryStyle.ink100
            opacity: 0.6
            SequentialAnimation on opacity {
                running: root.animate
                loops: Animation.Infinite
                PauseAnimation { duration: index * 700 }
                NumberAnimation { to: 1; duration: 1500; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.2; duration: 1500; easing.type: Easing.InOutSine }
            }
        }
    }

    // ---- причалы, маяк, буй, солнце ----
    Image {
        visible: root.mode === "dock" || root.mode === "seal"
        source: root.img("pier-left")
        x: 0
        width: root.mode === "dock" ? 100 : 80; height: width * 0.4
        y: root.height - height - (root.mode === "dock" ? 18 : 12)
        sourceSize: Qt.size(width * 2, height * 2)
    }
    Image {
        visible: root.mode === "door"
        source: root.img("pier-right")
        width: 80; height: 32
        x: root.width - width
        y: root.height - height - 12
        sourceSize: Qt.size(160, 64)
    }
    Image {
        visible: root.mode === "door"
        source: root.img("buoy")
        width: 22; height: 31
        x: 150; y: root.height - height - 9
        sourceSize: Qt.size(44, 62)
    }
    Image {
        visible: root.mode === "sail" || root.mode === "wait"
        source: root.img("lighthouse")
        width: root.mode === "sail" ? 20 : 18; height: width * 1.5
        x: root.width - width - 10
        y: root.height - height - (root.mode === "sail" ? 20 : 16)
        sourceSize: Qt.size(40, 60)
    }
    Image {
        visible: root.mode === "home"
        source: root.img("sun")
        width: 60; height: 60
        x: 40; y: root.height - 6 - 60 + 26
        sourceSize: Qt.size(120, 120)
    }
    Image {
        visible: root.mode === "home"
        source: root.img("pier-cargo")
        width: 160; height: 40
        x: root.width - width; y: root.height - height - 18
        sourceSize: Qt.size(320, 80)
    }
    Image {
        visible: root.mode === "seeds" || root.mode === "empty"
        source: root.img("pier-empty")
        width: 128; height: 32
        x: 0; y: root.height - height - (root.mode === "empty" ? 10 : 12)
        sourceSize: Qt.size(256, 64)
    }

    // ---- туман ----
    Repeater {
        model: root.mode === "fog" ? [[-30, 60, 16, 9, 0.10], [70, -30, 30, 11, 0.12], [-20, 120, 42, 8, 0.09]] : []
        delegate: Rectangle {
            required property var modelData
            required property int index
            x: modelData[0]
            width: root.width - modelData[0] - modelData[1]
            y: modelData[2]; height: modelData[3]
            radius: height / 2
            color: Qt.rgba(168 / 255, 183 / 255, 194 / 255, modelData[4])
            SequentialAnimation on x {
                running: root.animate
                loops: Animation.Infinite
                NumberAnimation { to: modelData[0] + 18; duration: 4500 + index * 900; easing.type: Easing.InOutSine }
                NumberAnimation { to: modelData[0]; duration: 4500 + index * 900; easing.type: Easing.InOutSine }
            }
        }
    }

    // ---- паромы ----
    component Boat: Item {
        id: boat
        property string art: "boat"
        property bool bob: true
        Image {
            id: pic
            anchors.fill: parent
            source: root.img(boat.art)
            sourceSize: Qt.size(boat.width * 2, boat.height * 2)
            transformOrigin: Item.Bottom
            SequentialAnimation on rotation {
                running: root.animate && boat.bob
                loops: Animation.Infinite
                NumberAnimation { from: -1; to: 1; duration: 2000; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1; to: -1; duration: 2000; easing.type: Easing.InOutSine }
            }
            SequentialAnimation on y {
                running: root.animate && boat.bob
                loops: Animation.Infinite
                NumberAnimation { from: 0; to: -2; duration: 2000; easing.type: Easing.InOutSine }
                NumberAnimation { from: -2; to: 0; duration: 2000; easing.type: Easing.InOutSine }
            }
        }
    }

    Boat {
        visible: root.mode === "dock"
        x: 86; width: 96; height: 48
        y: root.height - height - 14
    }
    Boat {
        visible: root.mode === "seal"
        art: "boat-seal"
        x: 72; width: 80; height: 40
        y: root.height - height - 10
    }
    Boat {
        visible: root.mode === "door"
        x: 26; width: 80; height: 40
        y: root.height - height - 10
    }
    Boat {
        visible: root.mode === "home"
        art: "boat-home"
        width: 88; height: 44
        x: root.width - 70 - width
        y: root.height - height - 12
    }
    Boat {
        visible: root.mode === "fog"
        art: "boat-ghost"
        bob: false
        opacity: 0.28
        x: 120; width: 80; height: 40
        y: root.height - height - 12
    }
    Boat {
        visible: root.mode === "seeds"
        art: "boat-ghost"
        bob: false
        opacity: 0.45
        width: 34; height: 17
        x: root.width - width - 8
        y: root.height - height - 26
    }
    Repeater {
        model: root.mode === "seeds" ? [38, 92] : []
        delegate: Boat {
            required property var modelData
            art: "boat-seed"
            x: modelData; width: 52; height: 26
            y: root.height - height - 16
        }
    }

    // ---- линия пути: паром идёт к маяку по мере приёма ----
    Item {
        id: lane
        visible: root.mode === "sail" || root.mode === "wait"
        x: root.mode === "sail" ? 18 : 16
        width: root.width - x - (root.mode === "sail" ? 40 : 36)
        y: root.height - (root.mode === "sail" ? 26 : 22)
        height: 0

        Row {
            y: -1
            spacing: 3
            Repeater {
                model: Math.max(0, Math.floor(lane.width / 5))
                Rectangle { width: 2; height: 2; color: Qt.rgba(126 / 255, 147 / 255, 162 / 255, 0.45) }
            }
        }
        Rectangle {
            y: -1
            height: 3
            width: lane.width * Math.max(0, Math.min(1, root.progress))
            color: root.mode === "sail" ? FerryStyle.signal : FerryStyle.wait
            Behavior on width { NumberAnimation { duration: 900 } }
        }
        Boat {
            art: root.mode === "sail" ? "boat" : "boat-wait"
            width: root.mode === "sail" ? 88 : 76
            height: width / 2
            x: lane.width * Math.max(0, Math.min(1, root.progress)) - width * 0.92
            y: -height + (root.mode === "sail" ? 10 : 9)
            Behavior on x { NumberAnimation { duration: 900 } }
        }
    }

    // ---- волна ----
    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.mode === "dock" ? 22 : root.mode === "sail" ? 24 : root.mode === "home" ? 20
                : root.mode === "fog" || root.mode === "empty" ? 16 : 18
        clip: true
        Image {
            id: wave
            width: root.width * 2
            height: parent.height
            source: root.img("wave")
            fillMode: Image.Stretch
            sourceSize: Qt.size(1440, 80)
            // Путь волны повторяется каждые 60 из 720 единиц, так что сдвиг
            // на половину ширины ложится сам на себя — петля без шва.
            NumberAnimation on x {
                running: root.animate
                loops: Animation.Infinite
                from: 0; to: -root.width
                duration: root.mode === "sail" ? 12000 : 16000
            }
        }
    }
}
