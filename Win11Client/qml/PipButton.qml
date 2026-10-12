import QtQuick
import QtQuick.Controls.Basic
import MeetUp

// Круглая кнопка поверх видео: органы окна «картинка в картинке» и кнопка
// «вынести в окно» на плитках. Подложка — затемнение, а не поверхность темы:
// под кнопкой чужой кадр любой яркости, и читаться она обязана на любом —
// светлая плашка светлой темы на белом слайде просто пропала бы.
Item {
    id: root

    property string icon: ""
    property string tip: ""
    // Тумблер в положении «выключено» (снятый микрофон, снятый звук) — красная
    // подложка, как у дока главного окна.
    property bool alert: false
    property int size: 32
    // Подсказка над кнопкой — у кнопок нижнего края окна: всплывашка не
    // выходит за пределы окна, и снизу её бы обрезало.
    property bool tipAbove: false
    signal clicked()

    implicitWidth: size
    implicitHeight: size

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.alert
            ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, ma.containsMouse ? 1.0 : 0.88)
            : Qt.rgba(0, 0, 0, ma.pressed ? 0.82 : ma.containsMouse ? 0.7 : 0.52)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, ma.containsMouse ? 0.24 : 0.12)
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    AppIcon {
        anchors.centerIn: parent
        name: root.icon
        size: Math.round(root.size * 0.5)
        color: "#ffffff"
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
        // Двойной щелчок по кадру разворачивает окно на весь экран. Быстрые
        // два нажатия по кнопке — это два нажатия кнопки, а не жест по кадру.
        onDoubleClicked: root.clicked()
    }

    // Значки без подписи сами за себя не говорят — подсказка обязательна.
    ToolTip {
        visible: root.tip !== "" && ma.containsMouse
        delay: 500
        y: root.tipAbove ? -height - 6 : root.height + 6
        x: (root.width - width) / 2
        contentItem: Text {
            text: root.tip
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
        }
        background: Rectangle {
            radius: Theme.radiusXs
            color: Theme.surface3
            border.width: 1
            border.color: Theme.borderStrong
        }
    }
}
