import QtQuick
import QtQuick.Controls.Basic
import MeetUp

// Ползунок по дискретным позициям — для шкал, где деления неравномерны: 0,3 с,
// 0,5, 1, 2, 5, 10 — и особая крайняя «выкл». Линейный Slider для такого не
// годится: на шкале до десяти секунд позиция 0,3 занимала бы три процента
// дорожки, а «выключить» пряталось бы за последним делением, куда ещё надо
// попасть. Здесь каждая позиция — ровный шаг, под дорожкой стоят деления с
// подписями, ручка прилипает к ближайшему.
//
// model — [{ value, label }]. Текущее значение задаётся снаружи (value),
// выбор человека уходит сигналом picked(value); привязку ползунок не рвёт —
// как и у PercentSlider, источник правды снаружи (см. Connections ниже).
// offLast: true — последняя позиция означает «выключено»: заливка до ручки
// гаснет, а деление отделено от остальных чертой.
Item {
    id: root

    property var model: []
    property var value
    property bool offLast: false
    signal picked(var value)

    readonly property int count: model.length
    readonly property int currentIndex: {
        for (var i = 0; i < count; ++i)
            if (model[i].value === root.value) return i
        return 0
    }
    readonly property bool isOff: offLast && count > 0 && currentIndex === count - 1
    // Ручка ходит не от края до края, а от половины своей ширины: центры
    // делений считаются по той же формуле, чтобы стоять ровно под ней.
    readonly property int handleSize: 16
    function centerOf(index) {
        return count > 1
            ? handleSize / 2 + (width - handleSize) * index / (count - 1)
            : width / 2
    }

    width: parent ? parent.width : 0
    implicitHeight: 50
    height: implicitHeight
    opacity: enabled ? 1 : 0.62

    Slider {
        id: slider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 26
        from: 0
        to: Math.max(0, root.count - 1)
        stepSize: 1
        snapMode: Slider.SnapAlways

        // Присваивание, а не привязка: Slider на перетаскивании пишет value
        // сам и порвал бы её (см. ScreenStage). Источник правды доносим
        // руками при каждой смене снаружи — кроме момента, когда ручку тянут.
        Component.onCompleted: value = root.currentIndex
        Connections {
            target: root
            function onValueChanged() { if (!slider.pressed) slider.value = root.currentIndex }
            function onModelChanged() { if (!slider.pressed) slider.value = root.currentIndex }
        }
        onMoved: {
            var i = Math.round(value)
            if (i >= 0 && i < root.count && root.model[i].value !== root.value)
                root.picked(root.model[i].value)
        }

        Behavior on value {
            enabled: !slider.pressed
            NumberAnimation { duration: 140; easing.type: Easing.OutQuad }
        }

        background: Rectangle {
            x: 0
            anchors.verticalCenter: parent.verticalCenter
            width: slider.width
            height: 6
            radius: 3
            color: Theme.surface3
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: 3
                // На «выкл» заливка гаснет: дорожка, залитая до упора, читалась
                // бы как «максимум», а это противоположное.
                color: root.isOff ? Theme.borderStrong : Theme.accent
                Behavior on color { ColorAnimation { duration: Theme.durFast } }
            }
        }
        handle: Rectangle {
            x: slider.visualPosition * (slider.width - width)
            anchors.verticalCenter: parent.verticalCenter
            width: root.handleSize
            height: root.handleSize
            radius: root.handleSize / 2
            color: Theme.text
            border.width: 1
            border.color: Theme.border
        }
    }

    // Деления и подписи под дорожкой. Крайние подписи прижаты к своему делению
    // внутрь, иначе первая уезжала бы за левый край, а последняя — за правый.
    Repeater {
        model: root.count
        delegate: Item {
            id: mark
            required property int index
            readonly property bool isCurrent: index === root.currentIndex
            readonly property real cx: root.centerOf(index)

            x: cx - width / 2
            y: 26
            width: 44
            height: 24

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 0
                width: 2
                height: 5
                radius: 1
                color: mark.isCurrent ? Theme.accentInk : Theme.borderStrong
            }
            Text {
                id: lbl
                y: 8
                x: mark.index === 0 ? mark.width / 2 - 1
                 : mark.index === root.count - 1 ? mark.width / 2 + 1 - width
                 : (mark.width - width) / 2
                text: root.model[mark.index].label
                color: mark.isCurrent ? Theme.accentInk : Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.text2xs
                font.weight: mark.isCurrent ? Font.DemiBold : Font.Normal
            }
        }
    }

    // Черта перед «выкл»: крайняя позиция — отдельный режим, а не ещё одно
    // значение шкалы, и стоять в одном ряду с числами ей не положено.
    Rectangle {
        visible: root.offLast && root.count > 1
        x: root.centerOf(root.count - 1.5) - width / 2
        y: 22
        width: 1
        height: 14
        color: Theme.borderStrong
    }
}
