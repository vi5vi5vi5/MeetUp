import QtQuick
import QtQuick.Effects
import QtMultimedia
import MeetUp

// Сцена демонстрации экрана: кадр целиком (никакого кропа — обрезанный слайд
// бесполезен), плашка «кто показывает» и заглушка на время, пока первый
// опорный кадр в пути. Своя демонстрация берётся из превью захвата, чужая —
// из декодера полосы SCREEN_* (Media.attachScreen).
Item {
    id: root

    property var sid: 0          // id ведущего
    property bool isSelf: false
    property string sharerName: ""
    property bool live: false    // кадры реально идут

    Rectangle {
        id: box
        anchors.fill: parent
        radius: Theme.radiusLg
        // Фон сцены — тот же градиент, что у плиток (.stagebox у веба):
        // кадр вписывается целиком, и поля по краям должны выглядеть родными.
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.tileFrom }
            GradientStop { position: 1.0; color: Theme.tileTo }
        }

        Item {
            id: mediaBox
            anchors.fill: parent
            visible: root.live
            layer.enabled: true
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: boxMask
            }
            VideoOutput {
                id: out
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectFit
            }
        }

        Item {
            id: boxMask
            anchors.fill: parent
            layer.enabled: true
            visible: false
            Rectangle { anchors.fill: parent; radius: Theme.radiusLg; color: "white" }
        }

        // Рамка — поверх видео (см. VideoTile: дети рисуются над отрисовкой
        // самого Rectangle, и собственный border ушёл бы под картинку).
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            color: "transparent"
            border.width: 1
            border.color: Theme.border
        }

        // Пока кадров нет: ведущий уже закреплён, но опорный кадр ещё в пути.
        Text {
            visible: !root.live
            anchors.centerIn: parent
            text: "Демонстрация начинается…"
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
        }

        // Плашка «кто показывает» — как stage-chip у веба.
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
                    name: "screen"
                    size: 13
                    color: Theme.accentInk
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.isSelf
                        ? ("Вы демонстрируете " + (Screens.selectedLabel || "экран"))
                        : ("Демонстрация — " + (root.sharerName || "участник"))
                    color: Theme.dark ? "#ffffff" : Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                    font.weight: Font.Medium
                }
            }
        }
    }

    // Ведущий может смениться без пересоздания сцены (один отпустил слот,
    // другой занял), поэтому привязку переносим руками, а не в onCompleted.
    // Номер привязки — как в VideoTile: отцепляем строго своё.
    property var _boundId: 0
    property int _tok: 0
    property bool _boundSelf: false

    function bind() {
        if (root.isSelf) {
            root._tok = Media.attachScreenPreview(out.videoSink)
            root._boundSelf = true
            root.live = Qt.binding(function () { return Media.screenPreviewActive })
        } else if (root.sid) {
            root._tok = Media.attachScreen(root.sid, out.videoSink)
            root._boundId = root.sid
            root.live = false
        }
    }
    function unbind() {
        if (root._tok === 0) return
        if (root._boundSelf) Media.detachScreenPreview(root._tok)
        else if (root._boundId) Media.detachScreen(root._boundId, root._tok)
        root._boundSelf = false
        root._boundId = 0
        root._tok = 0
        root.live = false
    }

    onSidChanged: { unbind(); bind() }
    onIsSelfChanged: { unbind(); bind() }
    Component.onCompleted: bind()
    Component.onDestruction: unbind()

    Connections {
        target: Media
        function onScreenVideoChanged(id, active) {
            if (!root.isSelf && id === root.sid) root.live = active
        }
    }
}
