import QtQuick
import MeetUp

// Общие детали языка Ferry для листа погрузки и карточек: сегменты,
// квадратные тумблеры, мелкие подписи, текстовые действия.
Item {
    // Переключатель сегментами: выбранный — сигнальный оранжевый.
    component Seg: Rectangle {
        id: seg
        property var labels: []
        // Доли ширины сегментов: «без лимита» длиннее «1», и на экране с
        // масштабом 125 % в равную долю он не влезал.
        property var weights: []
        readonly property real _sum: {
            let s = 0
            for (let i = 0; i < labels.length; i++) s += weights.length === labels.length ? weights[i] : 1
            return s
        }
        property int current: 0
        signal picked(int index)
        height: 32
        radius: 4
        color: FerryStyle.surface300
        Row {
            x: 2; y: 2
            spacing: 2
            Repeater {
                model: seg.labels
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: (seg.width - 4 - 2 * (seg.labels.length - 1))
                           * (seg.weights.length === seg.labels.length ? seg.weights[index] : 1) / seg._sum
                    height: 28
                    radius: 2
                    color: index === seg.current ? FerryStyle.signal : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: index === seg.current ? FerryStyle.inkOnSignal : FerryStyle.ink200
                        font.family: FerryStyle.sans
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: seg.picked(index)
                    }
                }
            }
        }
    }

    // Тумблер с заголовком и пояснением; квадратный, как всё измеряющее у Ferry.
    component Toggle: Item {
        id: tg
        property string title: ""
        property string hint: ""
        property bool on: false
        property bool warnWhenOn: false
        property bool enabledState: true
        signal flipped()
        width: parent ? parent.width : 0
        height: Math.max(tgText.implicitHeight, 22) + 20
        opacity: enabledState ? 1 : 0.5
        Column {
            id: tgText
            y: 10
            width: parent.width - 50
            spacing: 2
            Text {
                width: parent.width
                text: tg.title
                color: FerryStyle.ink100
                font.family: FerryStyle.sans
                font.pixelSize: 15
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                text: tg.hint
                color: tg.warnWhenOn && tg.on ? FerryStyle.warnText : FerryStyle.ink300
                font.family: FerryStyle.sans
                font.pixelSize: 13
                lineHeight: 18
                lineHeightMode: Text.FixedHeight
                wrapMode: Text.Wrap
            }
        }
        Rectangle {
            x: parent.width - 38
            y: 12
            width: 38; height: 22
            radius: 2
            color: tg.on ? FerryStyle.signal : FerryStyle.surface200
            border.width: 1
            border.color: tg.on ? FerryStyle.signal : FerryStyle.line200
            Rectangle {
                x: tg.on ? 19 : 3
                y: 3
                width: 14; height: 14; radius: 1
                color: tg.on ? FerryStyle.inkOnSignal : FerryStyle.ink200
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: tg.enabledState ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (tg.enabledState) tg.flipped()
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: 1
            color: FerryStyle.line100
        }
    }

    component SmallLabel: Text {
        color: FerryStyle.ink300
        font.family: FerryStyle.mono
        font.pixelSize: 12
        font.letterSpacing: 1
    }

    component TextAction: Text {
        id: ta
        signal clicked()
        color: FerryStyle.signalText
        font.family: FerryStyle.sans
        font.pixelSize: 13
        font.weight: Font.DemiBold
        MouseArea {
            anchors.fill: parent
            anchors.margins: -4
            cursorShape: Qt.PointingHandCursor
            onClicked: ta.clicked()
        }
    }
}
