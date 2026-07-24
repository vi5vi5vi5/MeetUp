import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import MeetUp

// Адресная строка конференции: вставил ссылку — вошёл. Всю работу (код комнаты,
// ключ E2E, смена сервера) делает Link; здесь только поле и кнопка.
// Пустое поле + клик по стрелке = «вставить из буфера и перейти»: ссылку
// обычно только что скопировали, лишний Ctrl+V ни к чему.
Item {
    id: root

    implicitWidth: 420
    implicitHeight: 44

    // Ошибка разбора ссылки — своя, отказ сервера («комната не найдена») —
    // от Rooms: для пользователя это одна и та же неудачная попытка войти.
    readonly property string _error: Link.errorText !== "" ? Link.errorText : Rooms.errorText

    function go() {
        if (field.text.trim() === "") {
            var fromClipboard = Sys.pasteText()
            if (fromClipboard === "") {
                hint.flash("Скопируйте ссылку на конференцию — и вставьте сюда.")
                return
            }
            field.text = fromClipboard
        }
        Link.open(field.text)
    }

    // Вошли — поле пустое: вернувшись из конференции, человек видит чистую
    // строку, а не свою же старую ссылку.
    Connections {
        target: Rooms
        function onRoomReady() { field.text = "" }
    }

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: Theme.radiusPill
        color: Theme.surface2
        border.width: 1
        border.color: root._error !== "" ? Theme.danger
                    : field.activeFocus ? Theme.accentInk : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 2
            spacing: 8

            AppIcon {
                Layout.alignment: Qt.AlignVCenter
                name: "link"
                size: 16
                color: field.activeFocus ? Theme.accentInk : Theme.textFaint
            }

            AppInput {
                id: field
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                background: null            // фон рисует пилюля
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                placeholderText: "Вставьте ссылку на конференцию"
                onAccepted: root.go()
                onTextChanged: { Link.clearError(); Rooms.clearError() }
            }

            IconButton {
                Layout.alignment: Qt.AlignVCenter
                size: "sm"
                icon: "arrow-right"
                variant: field.text.trim() !== "" ? "accent" : "neutral"
                onClicked: root.go()
            }
        }
    }

    // Подсказка/ошибка под строкой. Абсолютно спозиционирована и поверх всего:
    // шапка не должна прыгать из-за одной строки текста.
    Text {
        id: hint
        anchors.top: pill.bottom
        anchors.topMargin: 4
        anchors.horizontalCenter: pill.horizontalCenter
        width: pill.width
        z: 10
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: root._error !== "" ? root._error : _ownText
        visible: text !== ""
        color: root._error !== "" ? Theme.danger : Theme.textFaint
        font.family: Theme.uiFont
        font.pixelSize: Theme.text2xs

        property string _ownText: ""
        function flash(t) { _ownText = t; hintReset.restart() }
        Timer { id: hintReset; interval: 3000; onTriggered: hint._ownText = "" }
    }
}
