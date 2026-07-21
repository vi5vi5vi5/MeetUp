import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Effects
import MeetUp

// Conference view: stage (top bar · adaptive tile grid · floating dock) plus a
// right-hand chat/participants panel. All state is local mock — no networking.
Item {
    id: root

    signal leaveRequested()

    property string roomCode: ""
    property string myName: ""

    // Микрофон и камера по умолчанию ВЫКЛЮЧЕНЫ: входим в комнату тихо и без
    // картинки, включаем осознанно (совпадает с дефолтами C++ движков).
    property bool micOn: false
    property bool camOn: false
    property int activeTab: 0   // 0 = chat, 1 = participants

    // Локальные тумблеры в одном месте — их дёргают и док, и горячие клавиши.
    function toggleMic()   { micOn = !micOn; Conf.setLocalState(micOn, camOn) }
    function toggleCam()   { camOn = !camOn; Conf.setLocalState(micOn, camOn) }
    function toggleSound()  { Audio.outputMuted = !Audio.outputMuted }
    function toggleShare()  {
        if (sharing) Conf.setScreenShare(false)
        else if (screenActive)
            notify("Демонстрацию уже ведёт " + (sharerName || "другой участник") + ".")
        else picker.open = true
    }

    // Демонстрация экрана: единственный источник правды — слот на сервере
    // (§4.3). Кнопка в доке отражает его, а не локальное «я нажал».
    readonly property bool screenActive: Conf.screenId !== 0
    readonly property bool sharing: screenActive && Conf.screenId === Conf.myId
    readonly property string sharerName: {
        if (sharing) return "вы"
        var p = Conf.participants.filter(function (x) { return x.id === Conf.screenId })
        return p.length > 0 ? p[0].name : ""
    }

    // Полноэкранный показ демонстрации: сцена занимает всё, шапка, плёнка,
    // док и панель убираются. Само окно разворачивает Main (там же F11 и Esc),
    // экран лишь просит; выход из полного экрана гасит и режим показа — так
    // Esc остаётся одной кнопкой на оба состояния, без спорящих сочетаний.
    readonly property bool fullScreen:
        Window.window ? Window.window.visibility === Window.FullScreen : false
    signal fullScreenRequested()

    property bool theater: false
    onFullScreenChanged: if (!fullScreen) theater = false

    function toggleTheater() {
        theater = !theater
        if (theater !== fullScreen) fullScreenRequested()
    }

    // Боковая панель: на широком окне она часть раскладки, на узком —
    // выезжает оверлеем по кнопке (иначе чат недоступен совсем).
    readonly property bool sideDocked: width > 760 && !fullScreen && !theater
    property bool panelOpen: false

    // Закрепление участника: он крупно на сцене, остальные — плёнкой сверху.
    property var pinnedId: null
    function togglePin(id) { pinnedId = (pinnedId === id ? null : id) }

    // Режим сцены: демонстрация экрана либо закреплённый участник. Демонстрация
    // старше — пока она идёт, крупно показываем именно её (как у веба).
    readonly property bool stageMode: screenActive || pinnedId !== null

    // Страницы плиток: в сетку больше девяти не пускаем.
    property int page: 0
    readonly property int perPage: 9
    readonly property int pageCount: Math.max(1, Math.ceil(Conf.participants.length / perPage))
    readonly property int curPage: Math.min(page, pageCount - 1)

    // Что рисуем в сетке (в режиме сцены — ничего, там свои плитки).
    readonly property var pageItems: stageMode ? []
        : Conf.participants.slice(curPage * perPage, curPage * perPage + perPage)
    // Закреплённый (0 или 1 элемент). Закрепление СТАРШЕ демонстрации: нажал
    // на лицо — видишь лицо (так же и в вебе), нажал ещё раз — вернулся экран.
    readonly property var pinnedItems: pinnedId === null ? []
        : Conf.participants.filter(function (p) { return p.id === root.pinnedId })
    // Плёнка: при закреплении — все, кроме закреплённого, иначе все.
    readonly property var filmItems: !stageMode ? []
        : (pinnedId !== null
            ? Conf.participants.filter(function (p) { return p.id !== root.pinnedId })
            : Conf.participants)

    // Честный бейдж эфира: считаем от момента СВОЕГО входа (начало эфира
    // разовой комнаты серверу неизвестно — вебу, впрочем, тоже).
    property bool linkCopied: false
    property double joinedAtMs: 0
    property double nowMs: Date.now()

    // «05:14», после часа — «1:05:14» (как на главной)
    function fmtDuration(ms) {
        var s = Math.max(0, Math.floor(ms / 1000))
        function two(n) { return (n < 10 ? "0" : "") + n }
        var h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60
        return (h ? h + ":" + two(m) : two(m)) + ":" + two(s % 60)
    }

    Timer {
        interval: 1000; repeat: true
        running: Conf.phase === "live"
        onTriggered: root.nowMs = Date.now()
    }
    Connections {
        target: Conf
        function onPhaseChanged() {
            if (Conf.phase === "live" && root.joinedAtMs === 0) root.joinedAtMs = Date.now()
        }
        // Закреплённый участник ушёл — снимаем закрепление, иначе сцена пуста.
        function onParticipantsChanged() {
            if (root.pinnedId !== null && root.pinnedItems.length === 0)
                root.pinnedId = null
        }
        // Началась демонстрация — показываем её, а не чьё-то закреплённое лицо
        // (закрепить обратно всегда можно кликом по плёнке). Конец
        // демонстрации закрывает и полноэкранный показ.
        function onScreenChanged() {
            if (Conf.screenId !== 0) root.pinnedId = null
            else if (root.theater) root.toggleTheater()
        }
        function onScreenBusy() {
            root.notify("Демонстрацию экрана уже ведёт другой участник.")
        }
    }
    Timer { id: linkCopyReset; interval: 1400; onTriggered: root.linkCopied = false }

    // ---- Горячие клавиши (глобальные, работают и вне фокуса окна) ----
    // Их регистрирует система (Hotkeys). Отключаем, когда: открыта модалка
    // настроек (там эти же клавиши назначают — бинд перехватывал бы сам себя)
    // или пользователь пишет в чат (иначе одиночная клавиша-бинд съедалась бы
    // как символ). В другом приложении заглушить нечем — там глобальная
    // клавиша и должна срабатывать, ради этого всё и делалось.
    readonly property bool hotkeysActive: !settings.open && !chatInput.activeFocus
    onHotkeysActiveChanged: Hotkeys.setActive(hotkeysActive)

    Component.onCompleted: {
        Conf.open(root.roomCode, root.myName)
        Hotkeys.setActive(hotkeysActive)
    }
    Component.onDestruction: {
        Hotkeys.setActive(false)     // вне конференции клавиши не занимаем
        Conf.leave()
    }

    Connections {
        target: Hotkeys
        function onMicHotkey()   { root.toggleMic() }
        function onSoundHotkey() { root.toggleSound() }
        function onCamHotkey()   { root.toggleCam() }
        function onConflict(seq) {
            root.notify("Сочетание " + seq + " занято другим приложением.")
        }
    }

    // ---------------------------------------------------------------- Side panel
    // Затемнение под выехавшей панелью — только на узком окне.
    Rectangle {
        visible: !root.sideDocked && root.panelOpen && !root.theater
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.45)
        z: 80
        MouseArea { anchors.fill: parent; onClicked: root.panelOpen = false }
    }

    Rectangle {
        id: side
        visible: (root.sideDocked || root.panelOpen) && !root.theater
        z: root.sideDocked ? 0 : 90
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: root.sideDocked ? 340 : Math.min(root.width - 40, 360)
        color: Theme.surface

        Rectangle {
            anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.left: parent.left
            width: 1; color: Theme.border
        }

        // Tabs
        Row {
            id: tabs
            height: 60
            anchors { top: parent.top; left: parent.left; right: parent.right }
            anchors.leftMargin: Theme.padPanel
            // На узком окне справа сидит крестик — оставляем ему место.
            anchors.rightMargin: root.sideDocked ? Theme.padPanel : 52
            spacing: 8

            Repeater {
                model: [ { label: "Чат", idx: 0 }, { label: "Участники", idx: 1 } ]
                delegate: Rectangle {
                    required property var modelData
                    anchors.verticalCenter: parent.verticalCenter
                    width: tabLabel.implicitWidth + 28
                    height: 34
                    radius: Theme.radiusPill
                    color: root.activeTab === modelData.idx ? Theme.accent : "transparent"
                    Text {
                        id: tabLabel
                        anchors.centerIn: parent
                        text: modelData.label
                        color: root.activeTab === modelData.idx ? Theme.accentFg : Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textSm
                        font.weight: Font.Bold
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeTab = modelData.idx
                    }
                }
            }
        }
        IconButton {            // закрыть выехавшую панель (узкое окно)
            visible: !root.sideDocked
            anchors.right: parent.right
            anchors.rightMargin: Theme.padPanel
            anchors.top: parent.top
            anchors.topMargin: 10
            size: "sm"
            icon: "x"
            onClicked: root.panelOpen = false
        }

        Rectangle {
            anchors.top: tabs.bottom; anchors.left: parent.left; anchors.right: parent.right
            height: 1; color: Theme.border
        }

        // ---- Chat tab ----
        Item {
            visible: root.activeTab === 0
            anchors { top: tabs.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }

            ListView {
                id: chatList
                anchors { top: parent.top; left: parent.left; right: parent.right; bottom: composer.top }
                anchors.margins: Theme.padPanel
                clip: true
                spacing: 12
                model: Conf.messages
                boundsBehavior: Flickable.StopAtBounds
                delegate: ChatMessage {
                    required property var modelData
                    width: chatList.width
                    author: modelData.author
                    text: modelData.text
                    time: modelData.time
                    self: modelData.self
                }
                // держим ленту прокрученной вниз при новом сообщении/истории
                onCountChanged: positionViewAtEnd()
                Component.onCompleted: positionViewAtEnd()
            }

            Rectangle {
                id: composer
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: composerRow.implicitHeight + 24
                color: Theme.surface
                Rectangle {
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    height: 1; color: Theme.border
                }

                RowLayout {
                    id: composerRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    IconButton { size: "sm"; icon: "paperclip"; variant: "neutral"; enabled: false }  // картинки — M5
                    AppInput {
                        id: chatInput
                        Layout.fillWidth: true
                        placeholderText: "Сообщение…"
                        enabled: Conf.phase === "live"
                        onAccepted: { Conf.sendChat(chatInput.text); chatInput.text = ""; }
                    }
                    IconButton {
                        size: "sm"; icon: "send"; variant: "accent"
                        enabled: Conf.phase === "live"
                        onClicked: { Conf.sendChat(chatInput.text); chatInput.text = ""; }
                    }
                }
            }
        }

        // ---- Participants tab ----
        ListView {
            visible: root.activeTab === 1
            anchors { top: tabs.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
            anchors.margins: 10
            clip: true
            spacing: 2
            model: Conf.participants
            boundsBehavior: Flickable.StopAtBounds
            delegate: Item {
                id: partRow
                required property var modelData
                // Своё состояние микрофона сервер нам не эхонит (participant_state
                // уходит только другим), поэтому у себя берём живой тумблер —
                // ровно как плитки. Иначе свой значок не менялся при мьюте.
                readonly property bool micLive: modelData.isSelf ? root.micOn : modelData.mic
                width: ListView.view.width
                height: 52
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radiusSm
                    color: hov.hovered ? Theme.surface2 : "transparent"
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 12
                    spacing: 10
                    Avatar {
                        name: modelData.name
                        source: modelData.isSelf ? Auth.avatarUrl : (modelData.avatarUrl || "")
                        size: 34
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.name + (modelData.isSelf ? "  · вы" : "")
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textSm
                        font.weight: Font.Medium
                    }
                    AppIcon {
                        name: partRow.micLive ? "mic" : "mic-off"
                        size: 16
                        color: partRow.micLive ? Theme.textMuted : Theme.danger
                    }
                }
                HoverHandler { id: hov }
            }
        }
    }

    // ------------------------------------------------------------------- Stage
    Item {
        id: stage
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
        anchors.right: root.sideDocked ? side.left : parent.right

        // Top bar
        Item {
            id: confbar
            visible: !root.theater          // показ демонстрации — без шапки
            height: visible ? 68 : 0
            anchors { top: parent.top; left: parent.left; right: parent.right }
            anchors.leftMargin: Theme.padStage
            anchors.rightMargin: Theme.padStage

            RowLayout {
                anchors.fill: parent
                spacing: 12

                Text {
                    // Может расти лишь до своей полной ширины, а под давлением —
                    // сжимается и обрезается многоточием (как .conf-title у веба),
                    // чтобы длинное название не толкало кнопки за край панели.
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: implicitWidth
                    elide: Text.ElideRight
                    text: Conf.roomTitle !== "" ? Conf.roomTitle
                        : (root.roomCode !== "" ? root.roomCode : MockData.roomTitle)
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    font.letterSpacing: -0.8
                }
                Badge {
                    visible: Conf.phase === "live"
                    tone: "live"; dot: true
                    text: "в эфире · " + root.fmtDuration(root.nowMs - root.joinedAtMs)
                }
                Badge {                     // задержка до сервера (туда-обратно)
                    visible: Conf.phase === "live" && Conf.ping >= 0
                    // до 80 мс — отлично, до 200 — терпимо, дальше беда (как у веба)
                    tone: Conf.ping < 80 ? "accent" : Conf.ping < 200 ? "muted" : "danger"
                    text: Conf.ping + " мс"
                }
                Badge { visible: false; tone: "accent"; text: "🔒 E2E" }  // вернём в M5 (шифрование)

                Item { Layout.fillWidth: true }

                IconButton { // поделиться ссылкой на комнату
                    size: "sm"
                    icon: root.linkCopied ? "check" : "copy"
                    variant: root.linkCopied ? "active" : "neutral"
                    onClicked: {
                        Sys.copyText(Sys.roomLink(root.roomCode))
                        root.linkCopied = true
                        linkCopyReset.restart()
                    }
                }
                IconButton { size: "sm"; icon: Theme.dark ? "sun" : "moon"; variant: "neutral"; onClicked: Theme.toggle() }
            }
        }

        // Tile grid
        // ---- Обычный режим: сетка плиток, страницами по девять ----
        GridLayout {
            id: grid
            visible: !root.stageMode
            anchors { top: confbar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
            anchors.leftMargin: Theme.padStage
            anchors.rightMargin: Theme.padStage
            anchors.topMargin: 4
            anchors.bottomMargin: 116   // clear the floating dock
            columnSpacing: Theme.gapGrid
            rowSpacing: Theme.gapGrid
            columns: Math.max(1, Math.ceil(Math.sqrt(Math.max(1, root.pageItems.length))))

            Repeater {
                model: root.pageItems
                delegate: VideoTile {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    pid: modelData.id
                    name: modelData.name
                    isSelf: modelData.isSelf
                    speaking: Conf.speakingIds.indexOf(modelData.id) >= 0
                    mic: modelData.isSelf ? root.micOn : modelData.mic
                    cam: modelData.isSelf ? root.camOn : modelData.cam
                    avatar: modelData.isSelf ? Auth.avatarUrl : (modelData.avatarUrl || "")
                    onClicked: root.togglePin(modelData.id)
                }
            }
        }

        // ---- Режим сцены: плёнка сверху, крупно — экран или закреплённый ----
        // В полноэкранном показе поля обнуляются: демонстрация занимает всё.
        ColumnLayout {
            visible: root.stageMode
            anchors { top: confbar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
            anchors.leftMargin: root.theater ? 0 : Theme.padStage
            anchors.rightMargin: root.theater ? 0 : Theme.padStage
            anchors.topMargin: root.theater ? 0 : 4
            anchors.bottomMargin: root.theater ? 0 : 116
            spacing: Theme.gapGrid

            Flickable {                       // плёнка камер — листается вбок
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                visible: root.filmItems.length > 0 && !root.theater
                contentWidth: filmRow.width
                contentHeight: height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: filmRow
                    height: parent.height
                    spacing: 10
                    Repeater {
                        model: root.filmItems
                        delegate: VideoTile {
                            required property var modelData
                            width: 168
                            height: 96
                            pid: modelData.id
                            name: modelData.name
                            isSelf: modelData.isSelf
                            speaking: Conf.speakingIds.indexOf(modelData.id) >= 0
                            mic: modelData.isSelf ? root.micOn : modelData.mic
                            cam: modelData.isSelf ? root.camOn : modelData.cam
                            avatar: modelData.isSelf ? Auth.avatarUrl : (modelData.avatarUrl || "")
                            onClicked: root.togglePin(modelData.id)
                        }
                    }
                }
            }

            // Демонстрация экрана занимает сцену целиком. Loader, а не visible:
            // пока сцены нет, не должно быть и привязки к декодеру.
            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // НЕ убирать: невидимый элемент раскладка пропускает, а вот
                // просто неактивный Loader остаётся её участником и с
                // fillHeight честно забирает свою долю высоты — из-за этого
                // закреплённый участник и оказывался в нижней половине сцены.
                visible: root.screenActive && root.pinnedId === null
                active: root.screenActive && root.pinnedId === null
                sourceComponent: ScreenStage {
                    sid: Conf.screenId
                    isSelf: root.sharing
                    sharerName: root.sharerName
                    expanded: root.theater
                    onExpandRequested: root.toggleTheater()
                }
            }

            Repeater {                        // сама сцена: ноль или одна плитка
                model: root.pinnedItems
                delegate: PinnedStage {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    pid: modelData.id
                    name: modelData.name
                    isSelf: modelData.isSelf
                    cam: modelData.isSelf ? root.camOn : modelData.cam
                    avatar: modelData.isSelf ? Auth.avatarUrl : (modelData.avatarUrl || "")
                    onClicked: root.pinnedId = null
                }
            }
        }

        // ---- Точки страниц: только в сетке и только если страниц больше одной
        Row {
            visible: !root.stageMode && root.pageCount > 1
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 108
            spacing: 8
            z: 30
            Repeater {
                model: root.pageCount
                delegate: Rectangle {
                    required property int index
                    width: index === root.curPage ? 22 : 8
                    height: 8
                    radius: 4
                    color: index === root.curPage ? Theme.accent : Theme.borderStrong
                    Behavior on width { NumberAnimation { duration: Theme.durFast } }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.page = index }
                }
            }
        }

        // ---- Кнопка чата: на узком окне панель скрыта, открыть её больше нечем
        IconButton {
            visible: !root.sideDocked && !root.panelOpen && !root.theater
            anchors.right: parent.right
            anchors.rightMargin: Theme.padStage
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 104
            z: 40
            icon: "chat"
            variant: "neutral"
            onClicked: root.panelOpen = true
        }

        // Floating dock
        ControlDock {
            visible: !root.theater
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 26
            micOn: root.micOn
            camOn: root.camOn
            soundOn: !Audio.outputMuted
            sharing: root.sharing
            onToggleMic: root.toggleMic()
            onToggleCam: root.toggleCam()
            onToggleSound: root.toggleSound()
            onToggleShare: root.toggleShare()
            onOpenSettings: settings.open = true
            onLeave: root.leaveRequested()
        }

        // Уведомление над доком (.notice у веба): пилюля с тенью, длинный
        // текст обрезается многоточием, а не растягивает её за края сцены.
        MultiEffect {
            visible: notice.visible
            source: notice
            anchors.fill: notice
            z: 44
            shadowEnabled: true
            shadowColor: Theme.shadowColor
            shadowVerticalOffset: 8
            shadowBlur: 0.8
            autoPaddingEnabled: true
        }
        Rectangle {
            id: notice
            visible: root.noticeText !== ""
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 100
            z: 45
            width: Math.min(noticeLabel.implicitWidth + 36, parent.width - 32)
            height: noticeLabel.implicitHeight + 20
            radius: Theme.radiusPill
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            Text {
                id: noticeLabel
                anchors.centerIn: parent
                width: parent.width - 36
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                text: root.noticeText
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
                font.weight: Font.Medium
            }
        }

        // Баннер «идёт переподключение»: связь оборвалась, попытки продолжаются
        // (phase остаётся "live", поэтому оверлей ниже не показывается).
        Badge {
            visible: Conf.reconnecting
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 74
            z: 60
            tone: "danger"
            dot: true
            text: "переподключение…"
        }

        Rectangle {
            anchors.fill: parent
            visible: Conf.phase !== "live"
            color: Theme.bg
            z: 50

            Column {
                anchors.centerIn: parent
                width: Math.min(360, parent.width - 48)
                spacing: 16

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    text: Conf.phase === "connecting" ? "Подключение…"
                        : Conf.phase === "waiting"    ? (Conf.roomTitle || "Комната") + " ещё не в эфире"
                        : Conf.phase === "gate"       ? "Комната защищена паролем"
                        : /* error */                   "Не удалось войти"
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Conf.errorText !== "" ? Theme.danger : Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textSm
                    visible: text !== ""
                    text: Conf.errorText !== "" ? Conf.errorText
                        : Conf.phase === "waiting" ? "Ждём, пока владелец откроет комнату. Мы войдём автоматически."
                        : ""
                }

                // Гейт пароля
                Field {
                    visible: Conf.phase === "gate"
                    width: parent.width
                    label: "Пароль комнаты"
                    AppInput {
                        id: gatePass
                        width: parent.width
                        isPassword: true
                        placeholderText: "••••••"
                        onAccepted: Conf.submitPassword(gatePass.text)
                    }
                }
                AppButton {
                    visible: Conf.phase === "gate"
                    width: parent.width
                    text: "Войти"
                    variant: "primary"
                    onClicked: Conf.submitPassword(gatePass.text)
                }

                // Фатальная ошибка — только выйти
                AppButton {
                    visible: Conf.phase === "error"
                    width: parent.width
                    text: "Назад"
                    variant: "secondary"
                    onClicked: root.leaveRequested()
                }
            }
        }
    }

    // Настройки: устройства, громкость/чувствительность, качество отправки.
    SettingsModal {
        id: settings
        onClosed: settings.open = false
    }

    // Выбор объекта демонстрации. Подтверждение лишь ЗАПРАШИВАЕТ слот — захват
    // начнётся, когда сервер пришлёт подтверждение (VideoEngine его и ждёт).
    ScreenPickerModal {
        id: picker
        onClosed: picker.open = false
        onConfirmed: {
            picker.open = false
            Conf.setScreenShare(true)
        }
    }

    // Короткое уведомление (занятый слот, сорвавшийся захват). Как .notice у
    // веба — пилюля над доком, а не бейдж в шапке.
    property string noticeText: ""
    function notify(text) { noticeText = text; noticeReset.restart() }
    Timer { id: noticeReset; interval: 3500; onTriggered: root.noticeText = "" }

    Connections {
        target: Media
        function onScreenError(text) { root.notify(text) }
    }
}

