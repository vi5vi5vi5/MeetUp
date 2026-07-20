import QtQuick
import QtQuick.Effects
import QtMultimedia
import MeetUp

// Сцена закреплённого участника (клик по плитке). Повторяет .stagebox веба:
// градиентный фон, кадр ЦЕЛИКОМ (contain, а не cover — крупный план нельзя
// обрезать), крупная аватарка вместо выключенной камеры, плашка-подсказка
// «нажмите, чтобы вернуть». Это НЕ увеличенная VideoTile: у плитки другой
// закон кадрирования и свой чип с именем.
Item {
    id: root

    property var pid: 0
    property string name: ""
    property string avatar: ""
    property bool isSelf: false
    property bool cam: true
    property bool live: false

    signal clicked()

    readonly property bool videoShown: cam && live

    Rectangle {
        id: box
        anchors.fill: parent
        radius: Theme.radiusLg
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.isSelf ? Theme.tileSelfFrom : Theme.tileFrom }
            GradientStop { position: 1.0; color: root.isSelf ? Theme.tileSelfTo : Theme.tileTo }
        }

        // Видео: маска скругления, как в VideoTile (clip у Rectangle углы не
        // повторяет), своё — зеркально.
        Item {
            id: mediaBox
            anchors.fill: parent
            visible: root.videoShown
            layer.enabled: true
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: boxMask
            }
            VideoOutput {
                id: out
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectFit
                transform: Scale {
                    origin.x: out.width / 2
                    xScale: root.isSelf ? -1 : 1
                }
            }
        }

        Item {
            id: boxMask
            anchors.fill: parent
            layer.enabled: true
            visible: false
            Rectangle { anchors.fill: parent; radius: Theme.radiusLg; color: "white" }
        }

        // Камеры нет: крупная аватарка (у веба .stage-ava — 20% ширины, но не
        // больше 168 и не меньше 72 пикселей).
        Avatar {
            visible: !root.videoShown && root.avatar !== ""
            anchors.centerIn: parent
            name: root.name
            source: root.avatar
            size: Math.max(72, Math.min(168, parent.width * 0.20))
        }
        Text {
            visible: !root.videoShown && root.avatar === ""
            anchors.centerIn: parent
            text: root.cam && !root.isSelf ? "Подключаем камеру…" : "Камера выключена"
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
            font.weight: Font.Medium
        }

        // Рамка — поверх видео (дети рисуются над отрисовкой самого Rectangle).
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            color: "transparent"
            border.width: 1
            border.color: Theme.border
        }

        Rectangle {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 12
            width: chipRow.implicitWidth + 20
            height: chipRow.implicitHeight + 12
            radius: Theme.radiusXs
            color: Theme.scrimChip

            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: 6
                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "video"
                    size: 13
                    color: Theme.dark ? "#ffffff" : Theme.text
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: (root.isSelf ? "Вы" : root.name) + " — нажмите, чтобы вернуть"
                    color: Theme.dark ? "#ffffff" : Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                    font.weight: Font.Medium
                }
            }
        }
    }

    // Привязка к медиадвижку — по номеру, см. VideoTile.
    property int _tok: 0
    property var _pid: 0

    Component.onCompleted: {
        if (isSelf) {
            _tok = Media.attachPreview(out.videoSink)
            live = Qt.binding(function () { return Media.previewActive })
        } else if (pid) {
            _pid = pid
            _tok = Media.attach(pid, out.videoSink)
        }
    }
    Component.onDestruction: {
        if (_tok === 0) return
        if (isSelf) Media.detachPreview(_tok)
        else Media.detach(_pid, _tok)
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.clicked() }

    Connections {
        target: Media
        function onVideoChanged(id, active) {
            if (!root.isSelf && id === root.pid) root.live = active
        }
    }
}
