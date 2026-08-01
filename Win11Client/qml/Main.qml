import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import MeetUp

ApplicationWindow {
    id: window

    width: 1280
    height: 800
    minimumWidth: 760
    minimumHeight: 540
    visible: true
    title: "MeetUp"
    color: Theme.bg

    Behavior on color { ColorAnimation { duration: Theme.durMed } }

    // Полноэкранный режим. Прежнее состояние запоминаем: окно могло быть
    // развёрнутым, и возвращать его «в окно» было бы обидно.
    readonly property bool fullScreen: visibility === Window.FullScreen
    property int _prevVisibility: Window.Windowed

    function toggleFullScreen() {
        if (window.fullScreen) {
            window.visibility = window._prevVisibility
        } else {
            window._prevVisibility = window.visibility
            window.visibility = Window.FullScreen
        }
    }

    Shortcut { sequences: ["F11"]; onActivated: window.toggleFullScreen() }
    Shortcut {
        sequences: ["Esc"]
        enabled: window.fullScreen      // вне полного экрана Esc не наш
        onActivated: window.toggleFullScreen()
    }

    // Account flow: login <-> register / anon-lobby -> home -> conference.
    // Peer screens use replace(); the conference is push()ed so leaving pops back.
    StackView {
        id: stack
        anchors.fill: parent
        initialItem: loginPage

        replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durMed } }
        pushEnter:    Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durMed } }
        popEnter:     Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durMed } }
    }

    // Ярлык написан неверно. Сказать об этом надо словами: иначе человек
    // видит обычное окно вместо своей комнаты и гадает, что не так. Плашка, а
    // не модальное окно, — программа при этом полностью работоспособна.
    Rectangle {
        id: cliError
        property bool dismissed: false
        visible: Cli.errorText !== "" && !dismissed
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: visible ? cliErrRow.implicitHeight + 20 : 0
        z: 100
        color: Theme.surface
        border.width: 1
        border.color: Theme.danger

        Row {
            id: cliErrRow
            anchors.centerIn: parent
            width: parent.width - 32
            spacing: 12
            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "info"; size: 16; color: Theme.danger
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 60
                wrapMode: Text.WordWrap
                text: Cli.errorText
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textXs
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                size: "sm"; icon: "x"
                onClicked: cliError.dismissed = true
            }
        }
    }

    // Вход/выход из аккаунта (и обычный login, и авто-вход, и logout).
    Connections {
        target: Auth
        function onLoggedIn() {
            // Если мы уже на home — не дублируем.
            if (stack.currentItem && stack.currentItem.objectName === "home") return;
            stack.replace(homePage);
        }
        function onLoggedOut() {
            MyRoom.reset();               // чужая комната не должна мигнуть следующему
            // Смена сервера по ссылке тоже гасит профиль, но экран там выбирает
            // Link (анонимное лобби или главная) — не мигаем формой входа.
            if (Link.switching) return;
            stack.replace(loginPage);     // выход всегда ведёт на форму входа
        }
    }


    // Старт. Порядок несущий: адрес сервера из аргументов выставляем ДО
    // проверки сессии, иначе куку спросили бы не у того сервера. Комнату при
    // этом не трогаем — с ней разберётся Link.openAs, он умеет и переключить
    // сервер, и войти гостем, и подхватить ключ из ссылки.
    Component.onCompleted: {
        if (Cli.server !== "" && Cli.room === "" && Cli.link === "")
            Sys.setServer(Cli.server)
        Auth.checkSession()
        Updates.check()
    }

    // Аргументы применяем после ответа о сессии: до него неизвестно имя, под
    // которым входить. Ответ приходит и на отказ — как раз тот случай, когда
    // ярлык ведёт на сервер без нашего аккаунта.
    Connections {
        target: Auth
        function onSessionChecked(ok) {
            if (!Cli.pending) return
            Cli.consume()                       // sessionChecked придёт ещё раз
            if (Cli.room !== "" || Cli.link !== "")
                Link.openAs(Cli.inviteLink(Sys.serverAddress), Cli.name)
        }
    }

    // Ключ-фраза из аргументов. Только после входа: фраза превращается в ключ
    // с кодом комнаты в соли, а до join_ok кода ещё нет.
    property bool _cliPhraseUsed: false
    Connections {
        target: Conf
        function onJoinOk() {
            if (window._cliPhraseUsed || Cli.phrase === "") return
            window._cliPhraseUsed = true
            Crypto.applyPhrase(Cli.phrase)
        }
    }

    // Комната готова (создана/проверена/доверенная) — открываем конференцию,
    // передавая код и имя как свойства нового экрана.
    Connections {
        target: Rooms
        function onRoomReady(code, name) {
            stack.push(confPage, { roomCode: code, myName: name });
        }
    }

    // Успешный вход в комнату — пополняем локальную историю для главной.
    Connections {
        target: Conf
        function onJoinedRoom(code, title) { History.record(code, title) }
    }

    Component {
        id: loginPage
        LoginScreen {
            onRegisterRequested: () => { Auth.clearError(); stack.replace(registerPage) }
            onAnonRequested: () => stack.replace(anonPage)
        }
    }

    Component {
        id: registerPage
        RegisterScreen {
            onSignInRequested: () => { Auth.clearError(); stack.replace(loginPage) }
        }
    }

    Component {
        id: anonPage
        AnonLobbyScreen {
            onSignInRequested: () => stack.replace(loginPage)
        }
    }

    Component {
        id: homePage
        HomeScreen {}
    }

    Component {
        id: confPage
        ConferenceScreen {
            // roomLeft вернёт прежний сервер, если ссылка уводила на чужой.
            onLeaveRequested: { stack.pop(); Link.roomLeft() }
            onFullScreenRequested: window.toggleFullScreen()
        }
    }
}
