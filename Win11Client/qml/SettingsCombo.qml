import QtQuick
import QtQuick.Controls.Basic
import MeetUp

// Тематизированный выпадающий список настроек. Жил внутри SettingsModal как
// вложенный component — теперь разделов восемь, и он нужен каждому.
// Модель — [{id, label, soon}]; выбор отдаётся сигналом picked, а не
// двусторонней привязкой: источник правды — свойство в AV, а не индекс комбо.
// soon=true помечает вариант, который сейчас недоступен: он остаётся в списке,
// но приглушён и не выбирается — как soon у SegmentedControl. Убирать такой
// вариант из списка нельзя: тогда человек ищет пропавшую настройку и не
// понимает, была она или ему померещилось.
ComboBox {
    id: combo

    property string selectedId: ""
    signal picked(string id)

    textRole: "label"
    valueRole: "id"
    height: 40

    onActivated: picked(currentValue)

    // Индекс следует за выбранным id — и при живом обновлении списка
    // устройств (подключили гарнитуру), и при первом открытии.
    function sync() {
        var i = indexOfValue(selectedId)
        currentIndex = i >= 0 ? i : 0
    }
    onSelectedIdChanged: sync()
    onModelChanged: sync()
    Component.onCompleted: sync()

    contentItem: Text {
        leftPadding: 12
        rightPadding: 30
        verticalAlignment: Text.AlignVCenter
        text: combo.displayText
        elide: Text.ElideRight
        color: Theme.text
        font.family: Theme.uiFont
        font.pixelSize: Theme.textSm
    }
    indicator: Text {
        x: combo.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: Theme.textMuted
        font.pixelSize: Theme.textSm
    }
    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.surface2
        border.width: 1
        border.color: combo.popup.visible ? Theme.accent : Theme.border
    }
    delegate: ItemDelegate {
        id: row
        required property var modelData
        required property int index
        readonly property bool isSoon: modelData.soon === true
        width: combo.width - 12
        height: 34
        // Недоступный вариант не нажимается и не подсвечивается наведением:
        // ItemDelegate с enabled=false игнорирует и щелчок, и hover.
        enabled: !row.isSoon
        opacity: row.isSoon ? 0.5 : 1
        contentItem: Item {
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - (row.isSoon ? chip.width + 8 : 0)
                verticalAlignment: Text.AlignVCenter
                text: row.modelData.label
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
            }
            SoonChip {
                id: chip
                visible: row.isSoon
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        background: Rectangle {
            radius: Theme.radiusXs
            color: row.highlighted ? Theme.surface3 : "transparent"
        }
        highlighted: combo.highlightedIndex === index
    }
    popup: Popup {
        y: combo.height + 4
        width: combo.width
        padding: 6
        background: Rectangle {
            radius: Theme.radiusSm
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderStrong
        }
        contentItem: ListView {
            clip: true
            implicitHeight: Math.min(contentHeight, 240)
            model: combo.popup.visible ? combo.delegateModel : null
            currentIndex: combo.highlightedIndex
        }
    }
}
