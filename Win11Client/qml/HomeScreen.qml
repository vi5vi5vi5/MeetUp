import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Shapes
import MeetUp

// Reproduces home.html: the logged-in home — greeting, personal-room card,
// "join another" quick actions, recent history, account menu, and the profile /
// create-room / room-settings modals. Живые данные: Auth (профиль), MyRoom
// (личная комната + alias), History (локальная история), Rooms (вход).
Item {
    id: root
    objectName: "home"

    readonly property var room: MyRoom.room
    readonly property string name: Auth.displayName

    // «05:14», после часа — «1:05:14» (fmtDuration веба)
    function fmtDuration(ms) {
        var s = Math.max(0, Math.floor(ms / 1000))
        function two(n) { return (n < 10 ? "0" : "") + n }
        var h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60
        return (h ? h + ":" + two(m) : two(m)) + ":" + two(s % 60)
    }

    // Личная комната: загрузка при входе + опрос раз в 10 с + сразу при возврате в окно.
    Component.onCompleted: MyRoom.refresh()
    Timer { interval: 10000; repeat: true; running: true; onTriggered: MyRoom.refresh() }
    Window.onActiveChanged: if (Window.active) MyRoom.refresh()

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
                    Avatar { anchors.verticalCenter: parent.verticalCenter; name: root.name; source: Auth.avatarUrl; size: 30 }
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
                            Avatar { name: root.name; source: Auth.avatarUrl; size: 38 }
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
                        // Настоящий выход: сервер гасит сессию, Main.qml вернёт на логин.
                        MenuItem { icon: "arrow-right"; label: "Выйти"; danger: true; onTriggered: { accountMenu.close(); Auth.logout() } }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- Stage
    Flickable {
        id: content
        anchors { top: topbar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        contentWidth: width
        contentHeight: stage.implicitHeight + 48
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: content.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        // Две колонки на широком окне, одна — на узком (<900); при нехватке
        // высоты страница листается, при достатке — центрируется по вертикали.
        GridLayout {
            id: stage
            columns: content.width > 900 ? 2 : 1
            columnSpacing: 44
            rowSpacing: 34
            width: content.width > 900 ? Math.min(content.width - Theme.padStage * 2, 1000)
                                       : Math.min(content.width - Theme.padStage * 2, 460)
            x: Math.round((content.width - width) / 2)
            y: Math.max(24, (content.height - implicitHeight) / 2)

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
                            Avatar { anchors.verticalCenter: parent.verticalCenter; name: root.name; source: Auth.avatarUrl; size: 26 }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: (MyRoom.exists && root.room.online === true ? "Вы в эфире" : "Вы онлайн").toUpperCase()
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

                // Personal room card (когда комната есть)
                Card {
                    visible: MyRoom.exists
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
                            width: parent.width - 170
                            elide: Text.ElideRight
                            text: root.room.title || ""
                            color: Theme.text
                            font.family: Theme.displayFont
                            font.pixelSize: Theme.textXl
                            font.weight: Font.Bold
                            font.letterSpacing: -0.5
                        }
                        Badge {
                            id: liveBadge
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            tone: root.room.online === true ? "live" : "muted"
                            dot: true
                            property double nowMs: Date.now()
                            text: root.room.online === true
                                  ? "в эфире · " + root.fmtDuration(nowMs - (root.room.live_since_ms || nowMs))
                                  : "не в эфире"
                        }
                        Timer { // секундная стрелка бейджа (LiveDuration веба)
                            interval: 1000; repeat: true
                            running: root.room.online === true
                            onTriggered: liveBadge.nowMs = Date.now()
                        }
                    }

                    Row { // стопка участников — только в эфире
                        visible: root.room.online === true
                        width: parent.width
                        spacing: 10
                        Row {
                            spacing: -8    // аватарки внахлёст (hu-stack)
                            Repeater {
                                model: (root.room.participant_names || []).slice(0, 4)
                                delegate: Avatar { required property var modelData; name: modelData; size: 30 }
                            }
                        }
                        Text {
                            visible: (root.room.participant_names || []).length > 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: "+" + ((root.room.participant_names || []).length - 4)
                            color: Theme.textMuted
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textXs
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Участники (" + (root.room.participants || 0) + ")"
                            color: Theme.textMuted
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textSm
                        }
                    }

                    LinkRow { width: parent.width; code: root.room.code || "" }

                    Flow { // actions — переносятся на след. строку, если не влезли
                        width: parent.width
                        spacing: 10
                        AppButton {
                            text: root.room.online === true ? "Войти в эфир" : "Открыть комнату"
                            variant: "primary"
                            iconRight: "arrow-right"
                            onClicked: Rooms.enter(root.room.code, Auth.displayName)
                        }
                        AppButton { text: "Настройки"; variant: "secondary"; icon: "settings"; onClicked: roomModal.open = true }
                        AppButton {
                            visible: root.room.online === true
                            text: "Завершить"; variant: "danger"; icon: "phone-off"
                            onClicked: MyRoom.closeRoom()
                        }
                    }

                    Text { // подпись, когда не в эфире (room-note веба)
                        visible: root.room.online !== true
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Эфир начинается с вашего входа"
                              + (root.room.password ? ", гости подключаются по паролю" : "")
                              + " — и продолжается, пока в комнате кто-то есть."
                        color: Theme.textFaint
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textXs
                    }
                }

                // Пустое состояние — «Создайте личную комнату» (hu-empty веба)
                Item {
                    id: emptySlot
                    visible: MyRoom.loaded && !MyRoom.exists   // не мигает, пока грузимся
                    Layout.fillWidth: true
                    implicitHeight: emptyCol.implicitHeight + 72

                    Shape { // пунктирная рамка — у Rectangle пунктира нет
                        anchors.fill: parent
                        ShapePath {
                            strokeColor: emptyHover.hovered ? Theme.accentInk : Theme.borderStrong
                            strokeWidth: 1.5
                            strokeStyle: ShapePath.DashLine
                            dashPattern: [5, 4]
                            fillColor: emptyHover.hovered ? Theme.surface2 : Theme.surface
                            // PathRectangle — не Item: размеры задаём явно, через id контейнера.
                            PathRectangle {
                                width: emptySlot.width
                                height: emptySlot.height
                                radius: Theme.radiusCard
                            }
                        }
                    }

                    Column {
                        id: emptyCol
                        anchors.centerIn: parent
                        width: parent.width - 48
                        spacing: 16
                        IconButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            icon: "plus"; variant: "accent"
                            onClicked: createModal.open = true
                        }
                        Column {
                            width: parent.width
                            spacing: 6
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: "Создайте личную комнату"
                                color: Theme.text
                                font.family: Theme.displayFont
                                font.pixelSize: 20
                                font.weight: Font.Bold
                                font.letterSpacing: -0.5
                            }
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: "Постоянная комната с вашей собственной ссылкой — поделитесь ей один раз и встречайтесь всегда по ней."
                                color: Theme.textMuted
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textSm
                            }
                        }
                    }
                    HoverHandler { id: emptyHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: createModal.open = true }
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
                        width: parent.width
                        text: "Создать конференцию"
                        variant: "ghost"
                        icon: "plus"
                        enabled: !Rooms.busy
                        onClicked: Rooms.createRoom(Auth.displayName)
                    }
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
                    visible: History.items.length > 0      // пустую карточку не показываем
                    Layout.fillWidth: true
                    spacing: 4

                    // «5 мин назад» стареет само — раз в минуту пересчитываем подписи.
                    Timer {
                        interval: 60000; repeat: true
                        running: root.visible && History.items.length > 0
                        onTriggered: History.refreshLabels()
                    }

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
                            TapHandler { onTapped: History.clear() }
                        }
                    }
                    Repeater {
                        model: History.items
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width
                            height: 40
                            radius: Theme.radiusMd
                            color: histHover.hovered ? Theme.surface2 : "transparent"

                            Row {
                                anchors.left: parent.left; anchors.leftMargin: 10
                                anchors.right: rightSide.left; anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                AppIcon { anchors.verticalCenter: parent.verticalCenter; name: "video"; size: 16; color: Theme.textMuted }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 26
                                    elide: Text.ElideRight
                                    text: modelData.title || modelData.code   // заголовок, а нет — код
                                    color: Theme.text
                                    font.family: Theme.monoFont
                                    font.pixelSize: Theme.textSm
                                }
                            }
                            Row {
                                id: rightSide
                                anchors.right: parent.right; anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 4
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: History.whenLabel(modelData.at)
                                    color: Theme.textFaint
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.text2xs
                                }
                                IconButton { // «убрать из истории» — появляется под курсором
                                    visible: histHover.hovered
                                    size: "sm"; icon: "x"
                                    onClicked: History.removeCode(modelData.code)
                                }
                            }

                            HoverHandler { id: histHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: Rooms.enter(modelData.code, Auth.displayName) }  // доверенный вход (№2)
                        }
                    }
                }

                Text { // подпись — честность про приватность (как у веба)
                    visible: History.items.length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Никто не запоминает, к каким комнатам вы подключались, — вся история хранится только на этом компьютере."
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                }
            }
        }
    }

    // ---------------------------------------------------------------- Modals
    FileDialog {
        id: avatarDialog
        title: "Выберите фото"
        nameFilters: ["Изображения (*.png *.jpg *.jpeg *.bmp *.webp)"]
        onAccepted: Auth.uploadAvatar(selectedFile)   // selectedFile: QUrl (file:///...)
    }

    AppModal {
        id: profileModal
        title: "Настройки профиля"
        subtitle: "Имя и фото, которые видят участники."
        modalWidth: 440
        onClosed: open = false
        // При каждом открытии — чистый лист: без чужих ошибок и в режиме просмотра.
        onOpenChanged: if (open) { Auth.clearError(); nameBlock.editing = false }

        Row {   // ---- аватар-редактор ----
            width: parent.width
            spacing: 16
            Avatar { name: root.name; source: Auth.avatarUrl; size: 88 }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Row {
                    spacing: 8
                    AppButton {
                        text: Auth.avatarUrl !== "" ? "Заменить" : "Загрузить фото"
                        variant: Auth.avatarUrl !== "" ? "secondary" : "primary"
                        size: "sm"
                        icon: Auth.avatarUrl === "" ? "plus" : ""
                        enabled: !Auth.busy
                        onClicked: avatarDialog.open()
                    }
                    AppButton {
                        visible: Auth.avatarUrl !== ""
                        text: "Удалить"; variant: "ghost"; size: "sm"; icon: "x"
                        enabled: !Auth.busy
                        onClicked: Auth.removeAvatar()
                    }
                }
                Text {
                    text: "PNG или JPG · видна другим участникам конференции"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.text2xs
                }
            }
        }

        Column {   // ---- отображаемое имя: просмотр <-> правка ----
            id: nameBlock
            property bool editing: false
            width: parent.width
            spacing: 6

            // Имя сохранилось на сервере — выходим из режима правки.
            Connections {
                target: Auth
                function onProfileSaved() { nameBlock.editing = false }
            }

            Text {
                text: "ОТОБРАЖАЕМОЕ ИМЯ"
                color: Theme.textFaint
                font.family: Theme.labelFont
                font.pixelSize: Theme.text2xs
                font.letterSpacing: 2
            }

            Row {   // просмотр
                visible: !nameBlock.editing
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
                    TapHandler {
                        onTapped: {
                            nameField.text = root.name
                            nameBlock.editing = true
                            nameField.forceActiveFocus()
                        }
                    }
                }
            }

            Row {   // правка
                visible: nameBlock.editing
                width: parent.width
                spacing: 8
                AppInput {
                    id: nameField
                    width: parent.width - saveNameBtn.width - 8
                    maximumLength: 40
                    onAccepted: Auth.updateDisplayName(text)          // Enter — сохранить
                    Keys.onEscapePressed: nameBlock.editing = false   // Esc — передумал
                }
                AppButton {
                    id: saveNameBtn
                    text: "Сохранить"; variant: "primary"; icon: "check"
                    enabled: !Auth.busy
                    onClicked: Auth.updateDisplayName(nameField.text)
                }
            }

            Text {   // ошибка модалки (имя или аватарка)
                visible: Auth.errorText !== ""
                width: parent.width
                wrapMode: Text.WordWrap
                text: Auth.errorText
                color: Theme.danger
                font.family: Theme.uiFont
                font.pixelSize: Theme.textXs
            }
        }
    }

    AppModal {
        id: createModal
        title: "Новая личная комната"
        subtitle: "Постоянная комната с вашей собственной ссылкой."
        modalWidth: 470
        onClosed: open = false
        onOpenChanged: if (open) {   // каждый раз — с чистого листа
            MyRoom.clearError()
            crCode.text = ""; crTitle.text = ""; crPass.text = ""
        }

        // Комната создана — закрываемся (иначе модалка переживёт успех).
        Connections {
            target: MyRoom
            function onCreated() { createModal.open = false }
        }

        Field {
            width: parent.width
            label: "Код комнаты (ссылка)"
            // Живой предпросмотр ссылки: печатаешь «Моя Комната» — видишь «-».
            hint: Sys.host + "/conference.html?room=" + (MyRoom.slugify(crCode.text) || "…")
            AppInput {
                id: crCode
                width: parent.width
                placeholderText: "anna-room"
                onTextChanged: MyRoom.clearError()
                onAccepted: crTitle.forceActiveFocus()
            }
        }
        Field {
            width: parent.width
            label: "Название конференции"
            AppInput {
                id: crTitle
                width: parent.width
                placeholderText: "Комната " + Auth.displayName
            }
        }
        Field {
            width: parent.width
            label: "Пароль (необязательно)"
            hint: "Оставьте пустым — вход без пароля."
            AppInput { id: crPass; width: parent.width; isPassword: true; placeholderText: "••••••" }
        }
        AppButton {
            width: parent.width
            text: "Создать комнату"
            variant: "primary"
            iconRight: "arrow-right"
            enabled: !MyRoom.busy
            // Название по умолчанию — из плейсхолдера, как у веба.
            onClicked: MyRoom.create(crCode.text,
                                     crTitle.text.trim() !== "" ? crTitle.text.trim()
                                                                : "Комната " + Auth.displayName,
                                     crPass.text)
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: MyRoom.errorText !== "" ? MyRoom.errorText
                                          : "Комната сохраняется на сервере — ссылка заработает сразу."
            color: MyRoom.errorText !== "" ? Theme.danger : Theme.textFaint
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
        }
    }

    AppModal {
        id: roomModal
        title: "Настройки комнаты"
        subtitle: "Ссылка, название и пароль для подключения."
        modalWidth: 470
        onClosed: open = false

        property bool editingPass: false
        property bool showPass: false
        property bool confirmDel: false
        onOpenChanged: if (open) {
            MyRoom.clearError()
            MyRoom.loadAliases()
            rsCode.text = root.room.code || ""
            rsTitle.text = root.room.title || ""
            editingPass = false; showPass = false; confirmDel = false
            aliasSection.formOpen = false
        }

        Connections {
            target: MyRoom
            function onSaved()   { roomModal.editingPass = false; roomModal.showPass = false }
            function onRemoved() { roomModal.open = false }
        }

        // ---- Ссылка / код ----
        Column {
            width: parent.width
            spacing: 6
            visible: root.room.online === true      // в эфире код менять нельзя
            Text {
                text: "ССЫЛКА ДЛЯ ПОДКЛЮЧЕНИЯ"
                color: Theme.textFaint
                font.family: Theme.labelFont
                font.pixelSize: Theme.text2xs
                font.letterSpacing: 2
            }
            LinkRow { width: parent.width; code: root.room.code || "" }
            Text {
                text: "Ссылку нельзя изменить, пока идёт конференция."
                color: Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.textXs
            }
        }
        Field {
            width: parent.width
            visible: root.room.online !== true
            label: "Ссылка для подключения"
            hint: Sys.host + "/conference.html?room=" + (MyRoom.slugify(rsCode.text) || "…")
            AppInput {
                id: rsCode
                width: parent.width
                // Enter или уход фокуса — сохранить, если код реально другой.
                onEditingFinished: {
                    var c = MyRoom.slugify(text)
                    if (c !== "" && c !== root.room.code) MyRoom.change({ code: c })
                }
            }
        }

        // ---- Название ----
        Field {
            width: parent.width
            label: "Название конференции"
            Row {
                width: parent.width
                spacing: 8
                AppInput {
                    id: rsTitle
                    width: parent.width - rsTitleSave.width - 8
                    onAccepted: rsTitleSave.clicked()
                }
                AppButton {
                    id: rsTitleSave
                    text: "Сохранить"; variant: "secondary"; icon: "check"
                    enabled: !MyRoom.busy
                    onClicked: {
                        var t = rsTitle.text.trim()
                        if (t !== "" && t !== root.room.title) MyRoom.change({ title: t })
                    }
                }
            }
        }

        // ---- Пароль ----
        Column {
            width: parent.width
            spacing: 8
            Text {
                text: "ПАРОЛЬ НА ВХОД"
                color: Theme.textFaint
                font.family: Theme.labelFont
                font.pixelSize: Theme.text2xs
                font.letterSpacing: 2
            }

            Row {   // просмотр: пароль есть
                visible: !roomModal.editingPass && (root.room.password || "") !== ""
                spacing: 10
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: roomModal.showPass ? root.room.password : "••••••"
                    color: Theme.text
                    font.family: Theme.monoFont
                    font.pixelSize: 14
                    font.letterSpacing: 2
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: roomModal.showPass ? "Скрыть" : "Показать"
                    color: Theme.accentInk
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                    font.weight: Font.DemiBold
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: roomModal.showPass = !roomModal.showPass }
                }
                AppButton {
                    text: "Изменить"; variant: "secondary"; size: "sm"
                    onClicked: { rsPass.text = root.room.password || ""; roomModal.editingPass = true }
                }
                AppButton {
                    text: "Удалить"; variant: "ghost"; size: "sm"; icon: "x"
                    enabled: !MyRoom.busy
                    onClicked: MyRoom.change({ password: "" })   // пустой пароль = снять
                }
            }

            Row {   // просмотр: пароля нет
                visible: !roomModal.editingPass && (root.room.password || "") === ""
                width: parent.width
                spacing: 10
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Пароль не установлен — вход свободный."
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textSm
                }
                AppButton {
                    text: "Добавить пароль"; variant: "secondary"; size: "sm"; icon: "plus"
                    onClicked: { rsPass.text = ""; roomModal.editingPass = true }
                }
            }

            Row {   // правка
                visible: roomModal.editingPass
                width: parent.width
                spacing: 8
                AppInput {
                    id: rsPass
                    width: parent.width - 150
                    isPassword: !roomModal.showPass
                    placeholderText: "Новый пароль"
                    onAccepted: MyRoom.change({ password: rsPass.text })
                }
                AppButton {
                    text: roomModal.showPass ? "Скрыть" : "Показать"
                    variant: "secondary"; size: "sm"
                    onClicked: roomModal.showPass = !roomModal.showPass
                }
                AppButton {
                    text: "ОК"; variant: "primary"; size: "sm"; icon: "check"
                    enabled: !MyRoom.busy
                    onClicked: MyRoom.change({ password: rsPass.text })
                }
            }
        }

        Text {   // ошибки PATCH — под секциями
            visible: MyRoom.errorText !== ""
            width: parent.width
            wrapMode: Text.WordWrap
            text: MyRoom.errorText
            color: Theme.danger
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
        }

        Rectangle { width: parent.width; height: 1; color: Theme.border }

        Column {   // ---- Ссылки-приглашения (AliasSection веба) ----
            id: aliasSection
            property bool formOpen: false
            width: parent.width
            spacing: 8

            Connections {   // создание удалось — форма закрывается
                target: MyRoom
                function onAliasCreated() { aliasSection.formOpen = false }
            }

            Item {   // заголовок + «Новая ссылка»
                width: parent.width
                implicitHeight: 28
                Text {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    text: "ССЫЛКИ-ПРИГЛАШЕНИЯ"
                    color: Theme.textFaint
                    font.family: Theme.labelFont
                    font.pixelSize: Theme.text2xs
                    font.letterSpacing: 2
                }
                AppButton {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    visible: MyRoom.aliasesLoaded && !aliasSection.formOpen && MyRoom.aliases.length < 5
                    text: "Новая ссылка"; variant: "secondary"; size: "sm"; icon: "plus"
                    onClicked: aliasSection.formOpen = true
                }
            }

            Text {   // ещё грузим
                visible: !MyRoom.aliasesLoaded
                text: "Загружаем…"
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
            }

            Text {   // пусто — объясняем, зачем это вообще
                visible: MyRoom.aliasesLoaded && MyRoom.aliases.length === 0 && !aliasSection.formOpen
                width: parent.width
                wrapMode: Text.WordWrap
                text: "Дополнительные ссылки на эту же комнату — со своим паролем, лимитом использований и списком допущенных. До 5 ссылок."
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
            }

            Repeater {   // строки-алиасы
                model: MyRoom.aliases
                delegate: Rectangle {
                    id: aliasRow
                    required property var modelData
                    property bool copied: false
                    width: parent.width
                    implicitHeight: aliasCol.implicitHeight + 20
                    radius: Theme.radiusMd
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.border
                    opacity: modelData.enabled ? 1 : 0.55       // выключенная — «пригашена»

                    Column {
                        id: aliasCol
                        x: 10; y: 10
                        width: parent.width - 20
                        spacing: 6

                        RowLayout {   // ссылка + копировать + удалить
                            width: parent.width
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: Sys.host + "/conference.html?room=" + modelData.code
                                color: Theme.text
                                font.family: Theme.monoFont
                                font.pixelSize: 12
                            }
                            IconButton {
                                size: "sm"
                                icon: aliasRow.copied ? "check" : "copy"
                                variant: aliasRow.copied ? "active" : "neutral"
                                onClicked: {
                                    Sys.copyText(Sys.roomLink(aliasRow.modelData.code))
                                    aliasRow.copied = true
                                    aliasCopyReset.restart()
                                }
                            }
                            IconButton { size: "sm"; icon: "x"; onClicked: MyRoom.deleteAlias(aliasRow.modelData.id) }
                        }

                        RowLayout {   // бейджи-правила + вкл/выкл
                            width: parent.width
                            spacing: 6
                            Badge { visible: !aliasRow.modelData.enabled; tone: "muted"; text: "выключена" }
                            Badge {
                                visible: (aliasRow.modelData.password || "") !== ""
                                tone: "muted"
                                text: "пароль: " + aliasRow.modelData.password
                            }
                            Badge {
                                visible: aliasRow.modelData.uses_left >= 0
                                tone: "accent"
                                text: "осталось входов: " + aliasRow.modelData.uses_left
                            }
                            Badge {
                                visible: (aliasRow.modelData.logins || []).length > 0
                                tone: "muted"
                                text: "только: " + (aliasRow.modelData.logins || []).slice(0, 3).join(", ")
                                      + ((aliasRow.modelData.logins || []).length > 3
                                         ? " и ещё " + ((aliasRow.modelData.logins || []).length - 3) : "")
                            }
                            Text {
                                visible: aliasRow.modelData.enabled && (aliasRow.modelData.password || "") === ""
                                text: "без пароля"
                                color: Theme.textFaint
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.text2xs
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: aliasRow.modelData.enabled ? "Выключить" : "Включить"
                                color: Theme.accentInk
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textXs
                                font.weight: Font.DemiBold
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: MyRoom.toggleAlias(aliasRow.modelData.id, !aliasRow.modelData.enabled) }
                            }
                        }
                    }
                    Timer { id: aliasCopyReset; interval: 1400; onTriggered: aliasRow.copied = false }
                }
            }

            Rectangle {   // форма новой ссылки (AliasCreateForm)
                visible: aliasSection.formOpen
                width: parent.width
                implicitHeight: aliasForm.implicitHeight + 24
                radius: Theme.radiusMd
                color: "transparent"
                border.width: 1
                border.color: Theme.border

                Column {
                    id: aliasForm
                    x: 12; y: 12
                    width: parent.width - 24
                    spacing: 10
                    Field {
                        width: parent.width
                        label: "Пароль ссылки"
                        hint: "Пусто — вход без пароля, даже если он есть у комнаты."
                        AppInput { id: alPass; width: parent.width }
                    }
                    Field {
                        width: parent.width
                        label: "Лимит использований"
                        hint: "Пусто — без лимита. Исчерпанная ссылка удаляется."
                        AppInput {
                            id: alUses
                            width: parent.width
                            placeholderText: "без лимита"
                            validator: IntValidator { bottom: 1 }   // фильтр ввода, не замена проверке
                        }
                    }
                    Field {
                        width: parent.width
                        label: "Кому доступна"
                        hint: "Логины через запятую. Пусто — всем; со списком анонимный вход закрыт."
                        AppInput { id: alLogins; width: parent.width; placeholderText: "ivan, anna" }
                    }
                    Row {
                        anchors.right: parent.right
                        spacing: 8
                        AppButton {
                            text: "Отмена"; variant: "secondary"; size: "sm"
                            onClicked: aliasSection.formOpen = false
                        }
                        AppButton {
                            text: "Создать ссылку"; variant: "primary"; size: "sm"; icon: "check"
                            enabled: !MyRoom.busy
                            onClicked: MyRoom.createAlias(alPass.text, alUses.text, alLogins.text)
                        }
                    }
                }
            }

            Text {   // предел
                visible: MyRoom.aliasesLoaded && MyRoom.aliases.length >= 5
                text: "Достигнут предел — 5 ссылок."
                color: Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.textXs
            }

            Text {   // ошибки секции — свои, не общие с комнатой
                visible: MyRoom.aliasError !== ""
                width: parent.width
                wrapMode: Text.WordWrap
                text: MyRoom.aliasError
                color: Theme.danger
                font.family: Theme.uiFont
                font.pixelSize: Theme.textXs
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.border }

        Row {   // ---- Удаление с подтверждением ----
            width: parent.width
            spacing: 10
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 220
                wrapMode: Text.WordWrap
                text: roomModal.confirmDel ? "Точно удалить? Ссылка перестанет работать."
                                           : "Комната больше не нужна?"
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
            }
            AppButton {
                visible: roomModal.confirmDel
                text: "Отмена"; variant: "secondary"; size: "sm"
                onClicked: roomModal.confirmDel = false
            }
            AppButton {
                text: roomModal.confirmDel ? "Да, удалить" : "Удалить комнату"
                variant: roomModal.confirmDel ? "danger" : "ghost"
                size: "sm"; icon: "x"
                enabled: !MyRoom.busy
                onClicked: roomModal.confirmDel ? MyRoom.remove() : roomModal.confirmDel = true
            }
        }
    }
}
