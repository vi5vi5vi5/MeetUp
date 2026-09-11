import QtQuick
import MeetUp

// Reproduces login.html: sign in, create account, or continue anonymously.
// «Войти» ходит на сервер через Auth (инструкция №1); остальные кнопки — навигация.
AuthScaffold {
    id: root

    signal registerRequested()
    signal anonRequested()

    heroTitle: "С возвращением<br/>в <font color='" + Theme.accentInk + "'>эфир</font>"
    heroSub: "Войдите в аккаунт, чтобы продолжить встречи, историю комнат и контакты."
    // Имя сервера вместо общей подписи: человек должен видеть, КУДА он
    // подключается, — особенно когда серверов у него несколько.
    heroMeta: Server.name !== "MeetUp" ? "Сервер «" + Server.name + "»"
                                       : "Открытая видеосвязь без установки"

    Field {
        width: parent.width
        label: "Логин или email"
        AppInput {
            id: loginInput
            width: parent.width
            placeholderText: "anna@meetup.dev"
        }
    }
    Field {
        width: parent.width
        label: "Пароль"
        AppInput {
            id: passInput
            width: parent.width
            isPassword: true
            placeholderText: "••••••••"
            onAccepted: Auth.login(loginInput.text, passInput.text)
        }
    }

    AppButton {
        width: parent.width
        text: "Войти"
        variant: "primary"
        iconRight: "arrow-right"
        enabled: !Auth.busy
        onClicked: Auth.login(loginInput.text, passInput.text)
    }
    // Кнопок, которые сервер всё равно отвергнет, быть не должно: отказ после
    // заполненной формы раздражает сильнее, чем отсутствие кнопки.
    AppButton {
        width: parent.width
        visible: Server.registration
        text: "Создать аккаунт"
        variant: "ghost"
        icon: "plus"
        onClicked: root.registerRequested()
    }

    Divider {
        width: parent.width
        label: "или"
        visible: Server.anonymousJoin
    }

    AppButton {
        width: parent.width
        visible: Server.anonymousJoin
        text: "Войти без аккаунта"
        variant: "secondary"
        icon: "arrow-right"
        onClicked: root.anonRequested()
    }

    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: Auth.errorText !== "" ? Auth.errorText
              : !Server.registration
                ? "Регистрация на этом сервере закрыта — войдите выданным логином."
                : "Аккаунт хранит ваше имя между встречами."
        color: Auth.errorText !== "" ? Theme.danger : Theme.textFaint   // <— красный при ошибке
        font.family: Theme.uiFont
        font.pixelSize: Theme.textXs
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Адрес сервера: по умолчанию прод, правка сохраняется между запусками.
    // Рядом — кнопка «что это за сервер»: человек выбирает адрес здесь, и
    // вопросы про сервер возникают здесь же.
    Field {
        width: parent.width
        label: "Сервер"
        hint: "Пусто — вернуть адрес по умолчанию."

        Item {
            width: parent.width
            implicitHeight: serverInput.implicitHeight

            AppInput {
                id: serverInput
                width: parent.width - 48
                placeholderText: "meetup.linkpc.net"
                // Не биндинг: первый же символ, набранный руками, рвёт его
                // насовсем, и поле переставало отражать реальный адрес.
                // Подставляем сами — при рождении и когда адрес сменился
                // (но не из-под курсора).
                Component.onCompleted: text = Sys.serverAddress
                Connections {
                    target: Sys
                    function onServerChanged() {
                        if (!serverInput.activeFocus) serverInput.text = Sys.serverAddress
                    }
                }
                // Enter или уход фокуса — применяем и показываем нормализованный вид.
                onEditingFinished: {
                    if (text !== Sys.serverAddress) {
                        Sys.setServer(text)
                        text = Sys.serverAddress
                        Auth.checkSession()   // вдруг на этом сервере жива сессия — сразу впустит
                        Server.refresh()      // у другого сервера и правила другие
                    }
                }
            }

            IconButton {
                anchors.right: parent.right
                anchors.verticalCenter: serverInput.verticalCenter
                size: "sm"
                icon: "info"
                // Зелёная точка — «сервер ответил». Она же отвечает на самый
                // частый немой вопрос этого экрана: адрес вообще рабочий?
                variant: Server.reachable ? "active" : "neutral"
                onClicked: serverModal.open = true
            }
        }
    }

    // Не в теле карточки, а в слое поверх страницы: модалка накрывает экран
    // целиком, и колонка карточки для неё — неподходящее место (см. AuthScaffold).
    overlay: ServerInfoModal {
        id: serverModal
        onClosed: serverModal.open = false
    }
}
