import QtQuick
import MeetUp

// Личная громкость участника — по правой кнопке на его плитке.
//
// Не по центру экрана, как остальные модалки: ручку громкости открывают
// щелчком по конкретному человеку и закрывают через секунду, и гнать ради
// этого взгляд в середину конференции незачем. Поэтому — карточка у самой
// плитки, но со скримом: щелчок мимо закрывает, а нижние слои в это время
// ничего не ловят (см. VideoTile про пассивный захват TapHandler).
Item {
    id: root

    property bool open: false
    property var pid: 0
    property string name: ""
    // Куда ставить карточку — точка щелчка в координатах этого слоя.
    property point anchorPoint: Qt.point(0, 0)

    anchors.fill: parent
    visible: open
    z: 150      // ниже настроек (200) и выше плиток

    function popup(id, who, pos) {
        pid = id
        name = who
        anchorPoint = pos
        open = true
    }

    // Скрим без затемнения: тут нечего подчёркивать, он нужен только чтобы
    // поймать щелчок мимо и не пустить его к плиткам.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: root.open = false
    }

    Rectangle {
        id: card
        // Держимся у точки щелчка, но не вылезаем за края экрана.
        x: Math.max(8, Math.min(root.anchorPoint.x, root.width - width - 8))
        y: Math.max(8, Math.min(root.anchorPoint.y, root.height - height - 8))
        width: 268
        height: col.implicitHeight + 28
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        // Щелчки по самой карточке до скрима не доходят.
        MouseArea { anchors.fill: parent }

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 10

            Text {
                width: parent.width
                elide: Text.ElideRight
                text: root.name === "" ? "Громкость участника" : ("Громкость · " + root.name)
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
                font.weight: Font.DemiBold
            }

            Row {
                width: parent.width
                spacing: 10

                // Быстрый ноль и возврат: до тишины и обратно чаще всего и
                // тянутся, а ползунком это два точных движения.
                IconButton {
                    id: muteBtn
                    anchors.verticalCenter: parent.verticalCenter
                    size: "sm"
                    icon: slider.value === 0 ? "volume-off" : "volume"
                    variant: slider.value === 0 ? "active" : "neutral"
                    property int lastValue: 100
                    onClicked: {
                        if (slider.value > 0) {
                            lastValue = slider.value
                            slider.value = 0
                        } else {
                            slider.value = lastValue > 0 ? lastValue : 100
                        }
                    }
                }

                PercentSlider {
                    id: slider
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - muteBtn.width - pct.width - parent.spacing * 2
                    value: 100
                    onMoved: Audio.setPeerVolume(root.pid, value)
                    // Ползунок ставится и кнопкой «тихо», и при открытии —
                    // onMoved ловит только руку, поэтому отправляем по значению.
                    onValueChanged: if (root.open) Audio.setPeerVolume(root.pid, value)
                }

                Text {
                    id: pct
                    anchors.verticalCenter: parent.verticalCenter
                    // Ширина фиксирована: иначе ползунок дёргался бы вслед за
                    // числом («0%» и «200%» разной ширины).
                    width: 38
                    horizontalAlignment: Text.AlignRight
                    text: slider.value + "%"
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.text2xs
                    font.weight: Font.DemiBold
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "Слышно только вам. Сбрасывается при выходе из комнаты."
                color: Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.text2xs
            }
        }
    }

    // Открываемся с текущим значением, а не с прошлым: между открытиями его
    // мог поменять сброс на входе в комнату.
    onOpenChanged: if (open) slider.value = Audio.peerVolume(pid)
}
