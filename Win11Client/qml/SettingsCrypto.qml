import QtQuick
import MeetUp

// Раздел «Шифрование» (веха M5). Разметка намеренно повторяет веб-клиент:
// фраза-ключ у всех участников одна, и вводить её люди будут в разных
// клиентах — расхождение в подписях тут дороже, чем кажется.
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    // Ключ живёт только пока идёт разговор: на диск он не пишется намеренно
    // (см. E2eController). Поле следует за состоянием — после «Отключить» в
    // нём не должна оставаться фраза, которая уже ничего не значит.
    property string draft: Crypto.phrase

    Connections {
        target: Crypto
        function onChanged() { page.draft = Crypto.phrase }
    }

    Rectangle {
        width: parent.width
        height: 46
        radius: Theme.radiusMd
        color: Theme.surface2
        border.width: 1
        border.color: Crypto.active ? Theme.accentInk : Theme.border

        Rectangle {
            id: lamp
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: Crypto.active ? Theme.accent : Theme.textFaint
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
        }
        Text {
            anchors.left: lamp.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: Crypto.active
                ? (Crypto.fromLink
                   ? "Шифрование включено ключом из ссылки — сервер видит только шифротекст."
                   : "Шифрование включено — сервер видит только шифротекст.")
                : "Шифрование выключено — сервер видит ваши медиа и переписку."
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
        }
    }

    Field {
        width: parent.width
        label: "Фраза-ключ"
        hint: Crypto.fromLink
            ? "Ключ пришёл по ссылке-приглашению — фраза для него не нужна. Своя фраза заменит его."
            : "Одна фраза у всех участников. Ключ не покидает компьютер: сервер получает только шифрованные пакеты."
        Item {
            width: parent.width
            height: 44

            AppInput {
                id: phraseInput
                anchors.left: parent.left
                anchors.right: onBtn.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                enabled: !Crypto.busy
                isPassword: true
                placeholderText: "Одна фраза на всю комнату"
                text: page.draft
                onTextChanged: page.draft = text
                onAccepted: if (page.draft.trim() !== "") Crypto.applyPhrase(page.draft)
            }
            AppButton {
                id: onBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                // «Считаем…» — это не вежливость: вывод ключа из фразы намеренно
                // медленный (150 000 итераций), и без подписи кнопка выглядит
                // залипшей.
                text: Crypto.busy ? "Считаем…" : (Crypto.active ? "Применить" : "Включить")
                variant: "primary"
                enabled: !Crypto.busy && page.draft.trim() !== ""
                onClicked: Crypto.applyPhrase(page.draft)
            }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    Field {
        width: parent.width
        label: "Когда шифрование включено"
        hint: "Ссылка вида …/room#k=… открывает комнату уже расшифрованной — тот же формат, что у веба. Ключ едет во фрагменте адреса, а фрагмент на сервер не отправляется."
        Row {
            id: keyActions
            width: parent.width
            spacing: 8
            enabled: Crypto.active
            opacity: Crypto.active ? 1.0 : 0.62

            property bool copied: false

            AppButton {
                width: (parent.width - 8) / 2
                text: keyActions.copied ? "Скопировано" : "Скопировать ссылку с ключом"
                variant: "secondary"
                size: "sm"
                onClicked: {
                    var url = Crypto.inviteWithKey()
                    if (url === "") return
                    Sys.copyText(url)
                    keyActions.copied = true
                    copyReset.restart()
                }
            }
            AppButton {
                width: (parent.width - 8) / 2
                text: "Отключить"
                variant: "ghost"
                size: "sm"
                onClicked: Crypto.disable()
            }
            Timer { id: copyReset; interval: 1800; onTriggered: keyActions.copied = false }
        }
    }
}
