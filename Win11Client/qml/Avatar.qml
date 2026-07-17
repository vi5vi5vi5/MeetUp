import QtQuick
import QtQuick.Effects
import MeetUp

// Круглая аватарка: фото по URL (source), а до его загрузки — или без него —
// инициалы. Круглая форма фото — маской через MultiEffect.
Item {
    id: root
    property string name: ""
    property string source: ""          // URL фото; "" — показать инициалы
    property real size: 40
    property color bg: Theme.surface3

    implicitWidth: size
    implicitHeight: size

    readonly property bool _photo: source !== "" && photo.status === Image.Ready

    readonly property string _initials: {
        var p = String(name).trim().split(/\s+/).filter(function (s) { return s.length > 0 })
        if (!p.length) return "?"
        if (p.length === 1) return p[0].substring(0, 2).toUpperCase()
        return (p[0][0] + p[1][0]).toUpperCase()
    }

    Rectangle {                          // подложка + инициалы (fallback)
        anchors.fill: parent
        radius: width / 2
        color: root.bg
        Text {
            visible: !root._photo
            anchors.centerIn: parent
            text: root._initials
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Math.round(root.size * 0.4)
            font.weight: Font.ExtraBold
        }
    }

    Image {                              // само фото — скрыто, рисует эффект ниже
        id: photo
        anchors.fill: parent
        visible: false
        source: root.source
        fillMode: Image.PreserveAspectCrop
        asynchronous: true               // сеть/диск не блокируют интерфейс
        sourceSize: Qt.size(256, 256)    // аватарки на сервере ровно такие
    }
    MultiEffect {                        // фото, обрезанное круглой маской
        anchors.fill: photo
        source: photo
        visible: root._photo
        maskEnabled: true
        maskSource: mask
    }
    Item {                               // маска: круг на прозрачном фоне
        id: mask
        anchors.fill: parent
        layer.enabled: true              // маской может быть только текстура
        visible: false
        Rectangle { anchors.fill: parent; radius: width / 2; color: "white" }
    }

    Rectangle {                          // тонкое кольцо поверх (как border у веба)
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: Theme.border
    }
}
