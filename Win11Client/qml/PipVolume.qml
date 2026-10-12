import QtQuick
import MeetUp

// Ручка громкости поверх видео в окне «картинка в картинке»: значок, который
// по наведению разъезжается в ползунок с процентами (как пилюля на сцене
// демонстрации). Чья это громкость — решает тот, кто ставит: личная громкость
// участника или громкость его демонстрации.
Rectangle {
    id: pill

    property int value: 100
    // Человек сдвинул ручку или нажал «тихо» — источник правды меняет снаружи.
    signal changeRequested(int percent)

    readonly property bool open: hover.hovered || slider.pressed
    property int lastValue: 100         // куда возвращаться из «тихо»

    height: 32
    width: open ? 188 : 32
    radius: height / 2
    color: Qt.rgba(0, 0, 0, open ? 0.7 : 0.52)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, open ? 0.22 : 0.12)
    clip: true

    Behavior on width { NumberAnimation { duration: Theme.durMed; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: Theme.durFast } }

    HoverHandler { id: hover }
    // Гасит двойной щелчок по пилюле: у окна он разворачивает кадр на весь
    // экран, и подвинуть ползунок туда-обратно значило бы уехать в полный
    // экран. Лежит ПОД ползунком и значком — они получают события первыми.
    MouseArea { anchors.fill: parent; onDoubleClicked: {} }

    Item {
        id: icon
        anchors.right: parent.right
        width: 32
        height: parent.height

        AppIcon {
            anchors.centerIn: parent
            name: pill.value === 0 ? "volume-off" : "volume"
            size: 16
            color: pill.value === 0 ? Qt.rgba(1, 1, 1, 0.6) : "#ffffff"
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (pill.value > 0) {
                    pill.lastValue = pill.value
                    pill.changeRequested(0)
                } else {
                    pill.changeRequested(pill.lastValue > 0 ? pill.lastValue : 100)
                }
            }
        }
    }

    Text {
        id: pct
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        // Ширина фиксированная: иначе ползунок дёргался бы вслед за числом.
        width: 34
        text: pill.value + "%"
        color: Qt.rgba(1, 1, 1, 0.75)
        font.family: Theme.uiFont
        font.pixelSize: Theme.text2xs
        font.weight: Font.DemiBold
        opacity: pill.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
    }

    PercentSlider {
        id: slider
        anchors.left: pct.right
        anchors.leftMargin: 4
        anchors.right: icon.left
        anchors.verticalCenter: parent.verticalCenter
        enabled: pill.open
        opacity: pill.open ? 1 : 0
        value: pill.value
        onMoved: pill.changeRequested(Math.round(value))
        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

        // Slider на перетаскивании присваивает value сам и рвёт привязку выше
        // (та же грабля, что на сцене демонстрации): правду доносим руками.
        Connections {
            target: pill
            function onValueChanged() { slider.value = pill.value }
        }
    }
}
