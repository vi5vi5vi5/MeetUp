import QtQuick
import QtQuick.Effects
import QtMultimedia
import MeetUp

// A participant tile. Live video (remote decode or the local camera preview)
// fills the tile when frames arrive; otherwise the participant's avatar photo
// (or initials) stands in; a 2px accent border marks the active speaker; a
// frosted chip carries the name and muted-mic indicator.
Item {
    id: root

    property string name: ""
    property string avatar: ""   // URL фото (аватарка сервера); "" — инициалы
    property bool mic: true
    property bool cam: true
    property bool speaking: false
    property bool isSelf: false
    property var pid: 0          // id участника — ключ привязки к Media
    property bool live: false    // Media сообщил: картинка реально идёт

    // Видео рисуем, когда камера включена и кадры реально приходят.
    readonly property bool videoShown: cam && live

    readonly property string _initials: {
        var parts = String(name).trim().split(/\s+/).filter(function (s) { return s.length > 0 })
        if (parts.length === 0) return "?"
        if (parts.length === 1) return parts[0].substring(0, 1).toUpperCase()
        return (parts[0].substring(0, 1) + parts[1].substring(0, 1)).toUpperCase()
    }

    Rectangle {
        id: tile
        anchors.fill: parent
        radius: Theme.radiusLg
        clip: true
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.isSelf ? Theme.tileSelfFrom : Theme.tileFrom }
            GradientStop { position: 1.0; color: root.isSelf ? Theme.tileSelfTo : Theme.tileTo }
        }
        // Рамки здесь НЕТ намеренно: дочерние элементы рисуются поверх
        // собственной отрисовки Rectangle, и видео перекрыло бы её. Рамку
        // рисует оверлей `frame` — последним, поверх видео.

        // Faint watermark initials (only while there is neither video nor photo).
        Text {
            visible: root.cam && !root.videoShown && root.avatar === ""
            anchors.centerIn: parent
            text: root._initials
            color: Qt.rgba(1, 1, 1, 0.10)
            font.family: Theme.displayFont
            font.pixelSize: Math.min(parent.width, parent.height) * 0.30
            font.weight: Font.Bold
        }

        // Аватарка (фото с сервера или инициалы) — пока нет живого видео.
        Avatar {
            visible: !root.videoShown && !(root.cam && root.avatar === "")
            anchors.centerIn: parent
            name: root.name
            source: root.avatar
            size: Math.min(parent.width, parent.height) * 0.34
        }

        // Живое видео: у чужих плиток — декодированные кадры (Media.attach),
        // у своей — превью камеры (Media.attachPreview), зеркально, как у веба.
        //
        // ВАЖНО: `clip` у Rectangle — прямоугольный ножницы-клип, скругление он
        // НЕ повторяет. Без маски углы видео оставались бы прямыми и закрывали
        // и скруглённые углы плитки, и рамку. Поэтому медиа-слой рендерится
        // через layer.effect со скруглённой маской.
        Item {
            id: mediaBox
            anchors.fill: parent
            visible: root.videoShown
            layer.enabled: true
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: tileMask
            }
            VideoOutput {
                id: out
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectCrop
                transform: Scale {
                    origin.x: out.width / 2
                    xScale: root.isSelf ? -1 : 1
                }
            }
        }

        // Маска скругления плитки (текстура для layer.effect выше).
        Item {
            id: tileMask
            anchors.fill: parent
            layer.enabled: true
            visible: false
            Rectangle { anchors.fill: parent; radius: Theme.radiusLg; color: "white" }
        }

        // Рамка и подсветка «говорит» — поверх видео, иначе их не видно.
        Rectangle {
            id: frame
            anchors.fill: parent
            radius: Theme.radiusLg
            color: "transparent"
            border.width: root.speaking ? 2 : 1
            border.color: root.speaking ? Theme.accent : Theme.border
            Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
        }

        // Frosted name chip (bottom-left).
        Item {
            id: chip
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 10
            width: chipRow.implicitWidth + 16
            height: chipRow.implicitHeight + 10

            // «Морозное стекло» (backdrop-filter: blur у веба): размытая копия
            // кадра под чипом. Слой сам обрезает вылезающего ребёнка по своим
            // границам, а layer.effect скругляет углы — получается блюр ровно
            // в форме плашки. Работает только при живом видео: по градиенту
            // размывать нечего.
            Item {
                anchors.fill: parent
                visible: root.videoShown
                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: chipMask
                }
                MultiEffect {
                    x: -chip.x            // сдвигаем плитку так, чтобы под
                    y: -chip.y            // чипом оказался нужный её кусок
                    width: tile.width
                    height: tile.height
                    source: mediaBox
                    blurEnabled: true
                    blur: 1.0
                    blurMax: 32
                }
            }
            Item {
                id: chipMask
                anchors.fill: parent
                layer.enabled: true
                visible: false
                Rectangle { anchors.fill: parent; radius: Theme.radiusXs; color: "white" }
            }

            Rectangle {                 // сам скрим — поверх размытия
                anchors.fill: parent
                radius: Theme.radiusXs
                color: Theme.scrimChip
            }

            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: 6
                AppIcon {
                    visible: !root.mic
                    anchors.verticalCenter: parent.verticalCenter
                    name: "mic-off"
                    size: 13
                    color: Theme.danger
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.name + (root.isSelf ? " · вы" : "")
                    color: Theme.dark ? "#ffffff" : Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                    font.weight: Font.Medium
                }
            }
        }
    }

    Component.onCompleted: {
        if (isSelf) {
            Media.attachPreview(out.videoSink)
            live = Qt.binding(function () { return Media.previewActive })
        } else if (pid) {
            Media.attach(pid, out.videoSink)
        }
    }
    Component.onDestruction: {
        if (isSelf) Media.detachPreview()
        else if (pid) Media.detach(pid)
    }

    Connections {
        target: Media
        function onVideoChanged(id, active) {
            if (!root.isSelf && id === root.pid) root.live = active
        }
    }
}
