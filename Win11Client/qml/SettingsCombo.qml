import QtQuick
import QtQuick.Controls.Basic
import MeetUp

// Тематизированный выпадающий список настроек. Жил внутри SettingsModal как
// вложенный component — теперь разделов восемь, и он нужен каждому.
// Модель — [{id, label}]; выбор отдаётся сигналом picked, а не двусторонней
// привязкой: источник правды — свойство в AV, а не индекс комбо.
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
        width: combo.width - 12
        height: 34
        contentItem: Text {
            verticalAlignment: Text.AlignVCenter
            text: row.modelData.label
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
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
