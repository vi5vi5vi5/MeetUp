import QtQuick
import MeetUp

// Портрет сервера одним блоком. Живёт в двух местах и потому вынесен сюда:
// окно «Сервер» на экране входа и раздел «О программе» в настройках — это одни
// и те же сведения, и расходиться им незачем.
//
// Всё, кроме времени ответа, — со слов самого сервера: проверить это снаружи
// нечем. Формулировки внизу выбраны так, чтобы это было видно.
Column {
    id: root

    width: parent ? parent.width : 0
    spacing: 16

    // Показывать ли заголовок с именем сервера. В окне он нужен, в разделе
    // настроек над блоком уже стоит свой — второй читался бы как повтор.
    property bool showHeader: true

    // Строка «ключ — значение»: ключ слева приглушённым, значение справа.
    component InfoRow: Item {
        id: line
        property string key: ""
        property string value: ""
        property color valueColor: Theme.text

        width: parent ? parent.width : 0
        height: 30

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * 0.5
            elide: Text.ElideRight
            text: line.key
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * 0.5 - 10
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            text: line.value
            color: line.valueColor
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
            font.weight: Font.DemiBold
        }
    }

    component SectionLabel: Text {
        color: Theme.textFaint
        font.family: Theme.labelFont
        font.pixelSize: Theme.text2xs
        font.letterSpacing: 1.4
        font.capitalization: Font.AllUppercase
        bottomPadding: 4
    }

    // ---- Кто это ----
    Row {
        visible: root.showHeader
        width: parent.width
        spacing: 12

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 44
            height: 44
            radius: Theme.radiusSm
            color: Theme.surface2
            border.width: 1
            border.color: Theme.border
            AppIcon {
                anchors.centerIn: parent
                name: "info"
                size: 20
                color: Theme.accentInk
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 56
            spacing: 3
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: Server.name
                color: Theme.text
                font.family: Theme.displayFont
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                elide: Text.ElideMiddle
                text: Sys.host
                color: Theme.textMuted
                font.family: Theme.monoFont
                font.pixelSize: Theme.textXs
            }
        }
    }

    // Отклик — единственное число здесь, которое мы измерили сами, а не
    // услышали от сервера. Поэтому оно и стоит первым и отдельно.
    Rectangle {
        width: parent.width
        height: 54
        radius: Theme.radiusSm
        color: Theme.surface2
        border.width: 1
        border.color: Theme.border

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: Server.reachable ? Theme.live : Theme.danger
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    text: Server.reachable ? "Сервер отвечает" : "Сервер не отвечает"
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textSm
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "время ответа на запрос"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.text2xs
                }
            }
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: Server.pingMs >= 0 ? Server.pingMs + " мс" : "—"
            color: Server.reachable ? Theme.accentInk : Theme.textFaint
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLg
            font.weight: Font.Bold
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- Что разрешено ----
    Column {
        width: parent.width
        spacing: 0
        SectionLabel { text: "Что здесь разрешено" }
        InfoRow {
            key: "Регистрация"
            value: Server.registration ? "открыта" : "закрыта"
            valueColor: Server.registration ? Theme.text : Theme.textMuted
        }
        InfoRow {
            key: "Вход без аккаунта"
            value: Server.anonymousJoin ? "разрешён" : "запрещён"
            valueColor: Server.anonymousJoin ? Theme.text : Theme.textMuted
        }
        InfoRow {
            key: "Разовые конференции"
            value: Server.anonymousRooms === "off" ? "выключены"
                   : Server.anonymousRooms === "account" ? "только из аккаунта"
                   : "создаёт кто угодно"
        }
        InfoRow {
            key: "Личных комнат на человека"
            value: String(Server.maxPersonalRooms)
        }
        InfoRow {
            key: "Демонстраций в комнате"
            value: String(Server.maxScreenShares)
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- Приватность и сборка ----
    Column {
        width: parent.width
        spacing: 0
        SectionLabel { text: "Приватность и сборка" }
        InfoRow {
            key: "Журнал подключений"
            value: !Server.loaded ? "не сообщает"
                   : Server.logsNames ? "ведётся" : "не ведётся"
            valueColor: Server.logsNames ? Theme.text : Theme.accentInk
        }
        InfoRow {
            key: "Сборка сервера"
            value: Server.versionCommit === ""
                   ? "не сообщает"
                   : Server.versionCommit + (Server.versionModified ? " · с правками" : "")
        }
    }

    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        text: "Всё, кроме времени ответа, — со слов самого сервера: проверить это "
              + "снаружи нечем. Содержимое разговора от этого и не зависит — за него "
              + "отвечает сквозное шифрование, которое работает на вашем устройстве."
        color: Theme.textFaint
        font.family: Theme.uiFont
        font.pixelSize: Theme.textXs
    }
}
