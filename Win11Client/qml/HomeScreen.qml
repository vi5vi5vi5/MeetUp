import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import MeetUp

// Reproduces home.html: the logged-in home — greeting, personal-room card,
// "join another" quick actions, recent history, account menu, and the profile /
// create-room / room-settings modals. Runs on MockData (no networking yet).
Item {
    id: root

    signal logoutRequested()
    signal openConference(string code)

    readonly property var room: MockData.personalRoom
    readonly property string name: Auth.displayName

    // Reusable dropdown row for the account menu.
    component MenuItem: Rectangle {
        property string icon: ""
        property string label: ""
        property bool danger: false
        signal triggered()
        width: parent ? parent.width : 0
        height: 40
        radius: Theme.radiusSm
        color: hh.hovered ? Theme.surface2 : "transparent"
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            AppIcon { anchors.verticalCenter: parent.verticalCenter; name: icon; size: 16; color: danger ? Theme.danger : Theme.text }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: label
                color: danger ? Theme.danger : Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
                font.weight: Font.DemiBold
            }
        }
        HoverHandler { id: hh; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: triggered() }
    }

    // ---------------------------------------------------------------- Top bar
    Item {
        id: topbar
        height: 64
        anchors { top: parent.top; left: parent.left; right: parent.right }
        anchors.leftMargin: Theme.padStage
        anchors.rightMargin: Theme.padStage

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "MEETUP"
            color: Theme.text
            font.family: Theme.labelFont
            font.pixelSize: Theme.textLg
            font.letterSpacing: 3
            font.weight: Font.Bold
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            spacing: 12

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                size: "sm"
                icon: Theme.dark ? "sun" : "moon"
                onClicked: Theme.toggle()
            }

            // Account chip -> dropdown menu
            Rectangle {
                id: chip
                anchors.verticalCenter: parent.verticalCenter
                width: chipRow.implicitWidth + 18
                height: 40
                radius: Theme.radiusPill
                color: chipHover.hovered ? Theme.surface2 : Theme.surface
                border.width: 1
                border.color: Theme.border

                Row {
                    id: chipRow
                    anchors.centerIn: parent
                    spacing: 9
                    Avatar { anchors.verticalCenter: parent.verticalCenter; name: root.name; size: 30 }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.name
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textSm
                        font.weight: Font.Bold
                    }
                    AppIcon { anchors.verticalCenter: parent.verticalCenter; name: "more"; size: 16; color: Theme.textMuted }
                }
                HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: accountMenu.open() }

                Popup {
                    id: accountMenu
                    y: chip.height + 8
                    x: chip.width - width
                    width: 230
                    padding: 6
                    background: Rectangle {
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                        radius: Theme.radiusMd
                    }
                    contentItem: Column {
                        spacing: 2
                        // header
                        Row {
                            width: parent.width
                            spacing: 10
                            bottomPadding: 10
                            Avatar { name: root.name; size: 38 }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                Text { text: root.name; color: Theme.text; font.family: Theme.uiFont; font.pixelSize: Theme.textSm; font.weight: Font.Bold }
                                Text { text: "Ваш аккаунт"; color: Theme.textMuted; font.family: Theme.uiFont; font.pixelSize: Theme.text2xs }
                            }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.border }
                        MenuItem { icon: "settings"; label: "Настройки профиля"; onTriggered: { accountMenu.close(); profileModal.open = true } }
                        MenuItem { icon: Theme.dark ? "sun" : "moon"; label: Theme.dark ? "Светлая тема" : "Тёмная тема"; onTriggered: Theme.toggle() }
                        Rectangle { width: parent.width; height: 1; color: Theme.border }
                        MenuItem { icon: "arrow-right"; label: "Выйти"; danger: true; onTriggered: root.logoutRequested() }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- Stage
    Item {
        id: content
        anchors { top: topbar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }

        RowLayout {
            id: stage
            anchors.centerIn: parent
            width: Math.min(content.width - Theme.padStage * 2, 1000)
            spacing: 44

            // ----- Left column: greeting + personal room -----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 525
                Layout.alignment: Qt.AlignTop
                spacing: 34

                // Hero
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    Rectangle { // hero badge
                        Layout.alignment: Qt.AlignLeft
                        implicitWidth: badgeRow.implicitWidth + 18
                        implicitHeight: 38
                        radius: Theme.radiusPill
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                        Row {
                            id: badgeRow
                            anchors.centerIn: parent
                            spacing: 8
                            Avatar { anchors.verticalCenter: parent.verticalCenter; name: root.name; size: 26 }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: (root.room && root.room.online ? "Вы в эфире" : "Вы онлайн").toUpperCase()
                                color: Theme.textMuted
                                font.family: Theme.labelFont
                                font.pixelSize: Theme.text2xs
                                font.letterSpacing: 2
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: "С ВОЗВРАЩЕНИЕМ"
                            color: Theme.textFaint
                            font.family: Theme.labelFont
                            font.pixelSize: Theme.text2xs
                            font.letterSpacing: 2
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            text: root.name
                            color: Theme.text
                            font.family: Theme.displayFont
                            font.pixelSize: 52
                            font.weight: Font.ExtraBold
                            font.letterSpacing: -1.8
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.topMargin: 16
                            Layout.maximumWidth: 380
                            text: "Ваша личная комната всегда под рукой — начните встречу в один клик или подключитесь к чужой конференции."
                            color: Theme.textMuted
                            font.family: Theme.uiFont
                            font.pixelSize: 15
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Personal room card
                Card {
                    Layout.fillWidth: true
                    elevated: true
                    spacing: 16

                    Item { // room head: title + status badge
                        width: parent.width
                        implicitHeight: Math.max(roomTitle.implicitHeight, 28)
                        Text {
                            id: roomTitle
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 160
                            elide: Text.ElideRight
                            text: root.room.title
                            color: Theme.text
                            font.family: Theme.displayFont
                            font.pixelSize: Theme.textXl
                            font.weight: Font.Bold
                            font.letterSpacing: -0.5
                        }
                        Badge {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            tone: root.room.online ? "live" : "muted"
                            dot: true
                            text: root.room.online ? "в эфире · " + root.room.liveSinceLabel : "не в эфире"
                        }
                    }

                    Row { // participants (online only)
                        visible: root.room.online
                        width: parent.width
                        spacing: 10
                        Row {
                            spacing: -8
                            Repeater {
                                model: root.room.participantNames
                                delegate: Avatar { required property var modelData; name: modelData; size: 30 }
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Участники (" + root.room.participants + ")"
                            color: Theme.textMuted
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textSm
                        }
                    }

                    LinkRow { width: parent.width; code: root.room.code }

                    RowLayout { // actions
                        width: parent.width
                        spacing: 10
                        AppButton {
                            text: root.room.online ? "Войти в эфир" : "Открыть комнату"
                            variant: "primary"
                            iconRight: "arrow-right"
                            onClicked: Rooms.enter(room.code, Auth.displayName)
                        }
                        AppButton { text: "Настройки"; variant: "secondary"; icon: "settings"; onClicked: roomModal.open = true }
                        AppButton { visible: root.room.online; text: "Завершить"; variant: "danger"; icon: "phone-off" }
                    }
                }
            }

            // ----- Right column: quick actions + history -----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 475
                Layout.alignment: Qt.AlignTop
                spacing: 20

                Card {
                    Layout.fillWidth: true
                    spacing: 12
                    Text {
                        text: "Другая встреча"
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: Theme.textXl
                        font.weight: Font.Bold
                        font.letterSpacing: -0.5
                    }
                    Field {
                        width: parent.width
                        label: "Код чужой комнаты"
                        hint: "Спросите код у организатора"
                        AppInput {
                            id: otherCode
                            width: parent.width
                            placeholderText: "Например: fRt7…"
                            onAccepted: Rooms.joinByCode(otherCode.text, Auth.displayName)
                            onTextChanged: Rooms.clearError()
                        }
                    }
                    AppButton {
                        width: parent.width
                        text: "Подключиться"
                        variant: "primary"
                        iconRight: "arrow-right"
                        enabled: !Rooms.busy
                        onClicked: Rooms.joinByCode(otherCode.text, Auth.displayName)

                    }
                    Divider { width: parent.width; label: "или" }
                    AppButton { 
                        width: parent.width; 
                        text: "Создать конференцию"; 
                        variant: "ghost"; 
                        icon: "plus" 
                        enabled: !Rooms.busy
                        onClicked: Rooms.createRoom(Auth.displayName)}
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: Rooms.errorText !== "" ? Rooms.errorText
                             : "Код новой конференции сгенерирует сервер."
                        color: Rooms.errorText !== "" ? Theme.danger : Theme.textFaint
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textXs
                    }
                }

                Card {
                    Layout.fillWidth: true
                    spacing: 4
                    Item {
                        width: parent.width
                        implicitHeight: 24
                        Text {
                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            text: "Недавние конференции"
                            color: Theme.text
                            font.family: Theme.displayFont
                            font.pixelSize: Theme.textLg
                            font.weight: Font.Bold
                        }
                        Text {
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            text: "Очистить"
                            color: Theme.accentInk
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textXs
                            font.weight: Font.DemiBold
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                        }
                    }
                    Repeater {
                        model: MockData.history
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width
                            height: 40
                            radius: Theme.radiusMd
                            color: histHover.hovered ? Theme.surface2 : "transparent"
                            Row {
                                anchors.left: parent.left; anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                AppIcon { anchors.verticalCenter: parent.verticalCenter; name: "video"; size: 16; color: Theme.textMuted }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.title
                                    color: Theme.text
                                    font.family: Theme.monoFont
                                    font.pixelSize: Theme.textSm
                                }
                            }
                            Text {
                                anchors.right: parent.right; anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.when
                                color: Theme.textFaint
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.text2xs
                            }
                            HoverHandler { id: histHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: Rooms.enter(modelData.code, Auth.displayName) }

                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- Modals
    AppModal {
        id: profileModal
        title: "Настройки профиля"
        subtitle: "Имя и фото, которые видят участники."
        modalWidth: 440
        onClosed: open = false

        Row { // avatar editor
            width: parent.width
            spacing: 16
            Avatar { name: root.name; size: 88 }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Row {
                    spacing: 8
                    AppButton { text: "Загрузить фото"; variant: "primary"; size: "sm"; icon: "plus" }
                }
                Text {
                    text: "PNG или JPG · видна другим участникам конференции"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.text2xs
                }
            }
        }
        Column {
            width: parent.width
            spacing: 4
            Text { text: "ОТОБРАЖАЕМОЕ ИМЯ"; color: Theme.textFaint; font.family: Theme.labelFont; font.pixelSize: Theme.text2xs; font.letterSpacing: 2 }
            Row {
                spacing: 10
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.name
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    font.letterSpacing: -0.5
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Изменить"
                    color: Theme.accentInk
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                    font.weight: Font.DemiBold
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
        }
    }

    AppModal {
        id: createModal
        title: "Новая личная комната"
        subtitle: "Постоянная комната с вашей собственной ссылкой."
        modalWidth: 470
        onClosed: open = false

        Field {
            width: parent.width
            label: "Код комнаты (ссылка)"
            hint: MockData.serverAddress + "/conference.html?room=anna-room"
            AppInput { width: parent.width; placeholderText: "anna-room" }
        }
        Field {
            width: parent.width
            label: "Название конференции"
            AppInput { width: parent.width; placeholderText: "Комната Игоря" }
        }
        Field {
            width: parent.width
            label: "Пароль (необязательно)"
            hint: "Оставьте пустым — вход без пароля."
            AppInput { width: parent.width; isPassword: true; placeholderText: "••••••" }
        }
        AppButton { width: parent.width; text: "Создать комнату"; variant: "primary"; iconRight: "arrow-right" }
    }

    AppModal {
        id: roomModal
        title: "Настройки комнаты"
        subtitle: "Ссылка, название и пароль для подключения."
        modalWidth: 470
        onClosed: open = false

        Column {
            width: parent.width
            spacing: 6
            Text { text: "ССЫЛКА ДЛЯ ПОДКЛЮЧЕНИЯ"; color: Theme.textFaint; font.family: Theme.labelFont; font.pixelSize: Theme.text2xs; font.letterSpacing: 2 }
            LinkRow { width: parent.width; code: root.room.code }
        }
        Field {
            width: parent.width
            label: "Название конференции"
            AppInput { width: parent.width; text: root.room.title }
        }
        Column {
            width: parent.width
            spacing: 8
            Text { text: "ПАРОЛЬ НА ВХОД"; color: Theme.textFaint; font.family: Theme.labelFont; font.pixelSize: Theme.text2xs; font.letterSpacing: 2 }
            Row {
                spacing: 10
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.room.password
                    color: Theme.text
                    font.family: Theme.monoFont
                    font.pixelSize: 14
                    font.letterSpacing: 2
                }
                AppButton { text: "Изменить"; variant: "secondary"; size: "sm" }
                AppButton { text: "Удалить"; variant: "ghost"; size: "sm"; icon: "x" }
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.border }
        AppButton { text: "Удалить комнату"; variant: "ghost"; size: "sm"; icon: "x" }
    }
}
