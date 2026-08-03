import QtQuick
import MeetUp

// Захват горячей клавиши. Клик по полю переводит его в «слушаю»: следующее
// сочетание становится биндом. Backspace/Delete очищают, Esc отменяет.
//
// Само сочетание собирает не QML, а Hotkeys из сырого ввода: событие Qt не
// различает сторону клавиши и об одиноком модификаторе не сообщает вовсе, а
// «правый Ctrl» отдельным биндом — ровно то, ради чего всё и сделано. Отсюда
// и порядок: аккорд из модификаторов приходит сигналом captured() на
// отпускании, сочетание с обычной клавишей — сразу на нажатии.
Item {
    id: kb
    property string label: ""
    property string value: ""
    signal picked(string seq)

    width: parent ? parent.width : 0
    height: 38
    property bool listening: false

    // Выйти из режима назначения, не назначив ничего.
    function stopListening() {
        Hotkeys.stopCapture()
        kb.listening = false
    }

    Text {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - box.width - 12
        elide: Text.ElideRight
        text: kb.label
        color: Theme.text
        font.family: Theme.uiFont
        font.pixelSize: Theme.textSm
        font.weight: Font.Medium
    }

    Rectangle {
        id: box
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 172
        height: 34
        radius: Theme.radiusSm
        color: Theme.surface2
        border.width: kb.listening ? 2 : 1
        border.color: kb.listening ? Theme.accent : Theme.border

        Text {
            anchors.centerIn: parent
            width: parent.width - 20
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: kb.listening ? "Нажмите сочетание…"
                : (kb.value !== "" ? Hotkeys.label(kb.value) : "Не задано")
            color: kb.listening ? Theme.accentInk
                : (kb.value !== "" ? Theme.text : Theme.textFaint)
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
            font.weight: Font.Medium
        }

        Keys.onShortcutOverride: function (event) { event.accepted = kb.listening }
        Keys.onPressed: function (event) {
            if (!kb.listening) return
            event.accepted = true
            if (event.key === Qt.Key_Escape) { kb.stopListening(); return }
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                kb.stopListening(); kb.picked("")
            }
            // Всё остальное соберёт Hotkeys — здесь его только не мешаем.
        }
        // Потерял фокус, не дослушав, — выходим из режима захвата.
        onActiveFocusChanged: if (!activeFocus && kb.listening) kb.stopListening()

        Connections {
            target: Hotkeys
            function onCaptured(seq) {
                if (!kb.listening) return
                kb.listening = false
                kb.picked(seq)
            }
        }

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            // Фокус забираем ПЕРВЫМ: если слушало соседнее поле, оно потеряет
            // фокус и снимет захват — сделай мы это после, оно сняло бы наш.
            onTapped: { box.forceActiveFocus(); kb.listening = true; Hotkeys.startCapture() }
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }
}
