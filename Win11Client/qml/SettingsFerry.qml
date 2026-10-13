import QtQuick
import QtQuick.Dialogs
import MeetUp

// Раздел «Файлы»: Ferry в чате — куда класть принятое, раздавать ли его
// тем, кто придёт позже, и каким релеям можно показывать свой адрес.
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    Field {
        width: parent.width
        label: "Папка для приёма"
        hint: "Сюда ложатся тома, принятые по ссылкам Ferry из чата. Пока том едет, рядом лежит "
            + "его недокачка «….ferry-part»: оборвалась связь или закрыли приложение — "
            + "приём продолжится с того же места."
        Row {
            width: parent.width
            spacing: 10
            Rectangle {
                width: parent.width - change.width - 10
                height: change.height
                radius: Theme.radiusSm
                color: Theme.surface2
                border.width: 1
                border.color: Theme.border
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 12
                    width: parent.width - 24
                    text: Ferry.displayPath(Ferry.downloadDir)
                    elide: Text.ElideMiddle
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textSm
                }
            }
            AppButton {
                id: change
                text: "Изменить"
                variant: "ghost"
                size: "sm"
                onClicked: folderDialog.open()
            }
        }
    }

    Field {
        width: parent.width
        label: "Раздавать скачанное"
        hint: Ferry.seedMode === "off"
            ? "Скачав том, вы сразу отключаетесь от релея. Если отправитель уйдёт, опоздавшим "
              + "взять том будет не у кого — Ferry ничего не хранит на сервере."
            : "Ferry ничего не хранит на сервере: когда отправитель уходит, том живёт, только пока "
              + "его раздаёт кто-то из скачавших. Раздаются только тома из этого чата и только "
              + "тем, у кого есть ссылка с ключом."
        SegmentedControl {
            width: parent.width
            current: Ferry.seedMode
            model: [ { id: "call", label: "Пока я в звонке" },
                     { id: "app",  label: "Пока открыто приложение" },
                     { id: "off",  label: "Не раздавать" } ]
            onPicked: function (id) { Ferry.seedMode = id }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    Field {
        width: parent.width
        label: "Доверенные релеи"
        hint: Ferry.trustedRelays.length === 0
            ? "Пока ни одного. Ссылку на незнакомый релей карточка в чате не открывает сама: "
              + "спросить релей о раздаче — значит показать ему свой адрес. Релей, который "
              + "указан в настройках сервера MeetUp, доверенный всегда."
            : "О ссылках на эти релеи карточка в чате спрашивает сама, не дожидаясь нажатия. "
              + "Релей из настроек сервера MeetUp доверенный всегда."
        Column {
            width: parent.width
            spacing: 8
            visible: Ferry.trustedRelays.length > 0
            Repeater {
                model: Ferry.trustedRelays
                delegate: Text {
                    required property string modelData
                    text: modelData
                    color: Theme.text
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.textSm
                }
            }
            AppButton {
                text: "Забыть все"
                variant: "ghost"
                size: "sm"
                onClicked: Ferry.forgetTrusted()
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Папка для приёма Ferry"
        currentFolder: "file:///" + Ferry.downloadDir
        onAccepted: Ferry.downloadDir = selectedFolder
    }
}
