import QtQuick
import MeetUp

// Захват горячей клавиши. Клик по полю переводит его в «слушаю»: следующая
// нажатая комбинация становится биндом. Backspace/Delete очищают, Esc отменяет.
// Пока слушаем, onShortcutOverride перехватывает даже клавиши, которые иначе
// сработали бы как глобальные сочетания (например F11), чтобы записать именно их.
Item {
    id: kb
    property string label: ""
    property string value: ""
    signal picked(string seq)

    width: parent ? parent.width : 0
    height: 38
    property bool listening: false

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
            text: kb.listening ? "Нажмите клавишу…"
                : (kb.value !== "" ? kb.value : "Не задано")
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
            if (event.key === Qt.Key_Escape) { kb.listening = false; return }
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                kb.picked(""); kb.listening = false; return
            }
            var seq = AV.sequenceFromEvent(event.key, event.modifiers)
            if (seq !== "") { kb.picked(seq); kb.listening = false }
            // одинокий модификатор/зарезервированная клавиша — ждём дальше
        }
        // Потерял фокус, не дослушав, — выходим из режима захвата.
        onActiveFocusChanged: if (!activeFocus) kb.listening = false

        TapHandler {
            onTapped: { kb.listening = true; box.forceActiveFocus() }
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }
}
