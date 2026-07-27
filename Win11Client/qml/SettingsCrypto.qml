import QtQuick
import MeetUp

// Раздел «Шифрование» (веха M5). Разметка намеренно повторяет веб-клиент:
// фраза-ключ у всех участников одна, и вводить её люди будут в разных
// клиентах — расхождение в подписях тут дороже, чем кажется.
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    Rectangle {
        width: parent.width
        height: 46
        radius: Theme.radiusMd
        color: Theme.surface2
        border.width: 1
        border.color: Theme.border

        Rectangle {
            id: lamp
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: Theme.textFaint
        }
        Text {
            anchors.left: lamp.right
            anchors.leftMargin: 10
            anchors.right: chip.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: "Шифрование выключено — сервер видит ваши медиа и переписку."
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
        }
        SoonChip {
            id: chip
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Field {
        width: parent.width
        label: "Фраза-ключ"
        hint: "Одна фраза у всех участников. Ключ не покидает компьютер: сервер получает только шифрованные пакеты."
        Item {
            width: parent.width
            height: 44
            enabled: false
            opacity: 0.62

            AppInput {
                anchors.left: parent.left
                anchors.right: onBtn.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                isPassword: true
                placeholderText: "Одна фраза на всю комнату"
            }
            AppButton {
                id: onBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "Включить"
                variant: "primary"
            }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    Field {
        width: parent.width
        label: "Когда шифрование включено"
        hint: "Ссылка вида …/room#k=… открывает комнату уже расшифрованной — тот же формат, что у веба."
        Row {
            width: parent.width
            spacing: 8
            enabled: false
            opacity: 0.62

            AppButton {
                width: (parent.width - 8) / 2
                text: "Скопировать ссылку с ключом"
                variant: "secondary"
                size: "sm"
            }
            AppButton {
                width: (parent.width - 8) / 2
                text: "Отключить"
                variant: "ghost"
                size: "sm"
            }
        }
    }
}
