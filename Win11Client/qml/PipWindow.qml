import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtMultimedia
import QtCore
import MeetUp

// «Картинка в картинке»: камера или демонстрация участника в отдельном окне
// поверх всех окон. Нужно ровно тогда, когда человек ушёл из MeetUp в другое
// приложение, — поэтому окно:
//
//   * не забирает фокус: нажать «тихо» или подвинуть его можно, не выбивая
//     курсор из чужого редактора (WindowDoesNotAcceptFocus);
//   * не прячется вместе с главным окном: transientParent пустой, иначе
//     Windows сворачивала бы его следом за хозяином — то есть ровно в тот
//     момент, ради которого оно существует;
//   * не в панели задач и не в Alt+Tab (Tool) — это часть звонка, а не
//     отдельная программа;
//   * держит пропорции кадра при растягивании и скругляется как окно
//     Windows 11 (PipHelper, на C++). Двигается свободно — без прилипания к
//     краям: окно оставляют где угодно, хоть на стыке двух мониторов.
//
// Кадры приходят по приоритетной привязке (Media.attachPip): пока окно
// открыто, плитка в главном окне показывает заглушку, и картинка рисуется
// ровно в одном месте.
Window {
    id: win

    // ---- что показываем ----
    property string kind: "cam"          // "cam" — камера, "screen" — демонстрация
    property var pid: 0
    property string name: ""
    property string avatar: ""
    property bool cam: true              // камера участника включена (для "cam")
    property bool mic: true              // его микрофон — значок в чипе
    property bool speaking: false
    // Участник ушёл или показ закончился: окно прощается и закрывается само.
    property bool ended: false
    // Какое это окно по счёту: второе встаёт не поверх первого, а выше.
    property int cascade: 0
    // Главное окно MeetUp: на его экране окно встаёт в первый раз, и его же
    // поднимает кнопка «Открыть MeetUp».
    property var hostWindow: null

    // ---- своё: мини-док ----
    property bool micOn: false
    property bool soundOn: true

    signal returnRequested()             // вернуть видео в главное окно (и закрыться)
    signal micToggleRequested()
    signal soundToggleRequested()
    signal showMainRequested()
    signal keyRequested()                // «Ввести ключ» с заслонки
    signal resetRequested()              // сброс буфера (режим разработчика)
    // Человек подвинул, растянул или развернул окно — оно теперь его, даже
    // если открылось само при сворачивании MeetUp (см. ConferenceScreen).
    signal touched()

    readonly property bool isScreen: kind === "screen"
    property bool live: false            // кадры реально идут
    property bool locked: false          // поток закрыт чужим ключом E2E
    readonly property bool videoShown: live && !locked && (isScreen || cam)
    readonly property bool full: visibility === Window.FullScreen

    // Имя переживает уход человека: прощание «Анна вышла» без имени было бы
    // «вышла» неизвестно кто.
    property string _lastName: ""
    onNameChanged: if (name !== "") _lastName = name

    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | Qt.Tool | Qt.WindowDoesNotAcceptFocus
    transientParent: null
    title: isScreen ? "Демонстрация — " + _lastName : _lastName
    color: "#0e0e10"
    minimumWidth: 240                    // те же числа — в PipHelper.cpp
    minimumHeight: 135
    visible: false                       // показываем, когда расставили

    // ---------------------------------------------------------------- Место
    // Размер и место помнятся отдельно для камер и для демонстраций: камеру
    // держат маленькой в углу, демонстрацию — крупнее и часто на втором
    // мониторе. Хранилище — QSettings, группа pip.
    //
    // «Место ещё не запоминали» — отдельным флагом, а не числом-маркером:
    // отрицательный маркер реестр хранит как беззнаковое 4294867296, и такое
    // «место» выглядело бы настоящим. Отрицательные координаты при этом
    // законны сами по себе — монитор слева от основного.
    Settings {
        id: store
        category: "pip"
        property bool camKnown: false
        property int camX: 0
        property int camY: 0
        property int camW: 360
        property int camH: 203
        property bool scrKnown: false
        property int scrX: 0
        property int scrY: 0
        property int scrW: 512
        property int scrH: 288
    }

    function place() {
        var w = win.isScreen ? store.scrW : store.camW
        var h = win.isScreen ? store.scrH : store.camH
        var x = win.isScreen ? store.scrX : store.camX
        var y = win.isScreen ? store.scrY : store.camY
        var known = win.isScreen ? store.scrKnown : store.camKnown
        // Монитор, на котором окно стояло в прошлый раз, могли отключить —
        // тогда место прошлого раза не годится, встаём в угол заново. Если
        // середина окна на каком-то экране есть, ставим ровно туда же, где
        // оставили, — даже наполовину за краем: так человек его и бросил.
        var wa = known ? PipHelper.workAreaAt(x + w / 2, y + h / 2)
                       : Qt.rect(0, 0, 0, 0)
        if (wa.width <= 0) {
            wa = PipHelper.workArea(win.hostWindow)
            w = Math.max(win.minimumWidth, Math.min(w, wa.width - 32))
            h = Math.max(win.minimumHeight, Math.min(h, wa.height - 32))
            x = wa.x + wa.width - w - 16
            y = wa.y + wa.height - h - 16
            // Второе и третье окно — выше первого, а не поверх него.
            y = Math.max(wa.y, y - win.cascade * (h + 12))
        } else if (win.cascade > 0) {
            // Место запомнено, но там уже стоит первое окно — чуть в сторону.
            x += win.cascade * 28
            y += win.cascade * 28
        }
        win.x = x
        win.y = y
        win.width = w
        win.height = h
    }

    function remember() {
        if (win.full) return
        if (win.isScreen) {
            store.scrX = win.x; store.scrY = win.y
            store.scrW = win.width; store.scrH = win.height
            store.scrKnown = true
        } else {
            store.camX = win.x; store.camY = win.y
            store.camW = win.width; store.camH = win.height
            store.camKnown = true
        }
    }

    Connections {
        target: PipHelper
        function onMoveFinished(w) {
            if (w !== win) return
            win.remember()
            win.touched()
        }
    }

    // ---------------------------------------------------------- Пропорции
    // Окно держит пропорции кадра (PipHelper), а первый кадр подгоняет под
    // них высоту: камера бывает 4:3, демонстрация — какая угодно, а полосы
    // по краям в маленьком окне съедают половину картинки.
    readonly property real srcAspect: out.sourceRect.width > 0 && out.sourceRect.height > 0
                                      ? out.sourceRect.width / out.sourceRect.height : 0
    onSrcAspectChanged: fitAspect()

    function fitAspect() {
        if (win.srcAspect <= 0) return
        PipHelper.setAspect(win, win.srcAspect)
        if (win.full) return
        var w = win.width
        var h = Math.round(w / win.srcAspect)
        if (h < win.minimumHeight) { h = win.minimumHeight; w = Math.round(h * win.srcAspect) }
        if (Math.abs(h - win.height) <= 1 && Math.abs(w - win.width) <= 1) return
        // Держим тот край, у которого окно стоит: в нижнем углу — нижний,
        // иначе окно уезжало бы за панель задач.
        var wa = PipHelper.workArea(win)
        var keepBottom = win.y + win.height / 2 > wa.y + wa.height / 2
        var bottom = win.y + win.height
        win.width = w
        win.height = h
        if (keepBottom) win.y = bottom - h
        win.remember()
    }

    function toggleFull() {
        win.touched()
        if (win.full) {
            win.visibility = Window.Windowed
        } else {
            win.visibility = Window.FullScreen
            win.wake()
        }
    }

    // ------------------------------------------------- Интерфейс по наведению
    // Органы появляются, когда над окном мышь, — чужой кадр в остальное время
    // ничем не закрыт. Во весь экран — правило плееров: стоит мышь, нет и
    // интерфейса (и курсора).
    property bool _idle: false
    readonly property bool chromeShown: win.ended || volume.open
        || (hover.hovered && !(win.full && win._idle))
    function wake() {
        win._idle = false
        if (win.full) idleTimer.restart()
    }
    Timer {
        id: idleTimer
        interval: 2200
        onTriggered: win._idle = !volume.open
    }
    onFullChanged: { idleTimer.stop(); win._idle = false }

    Item {
        id: content
        anchors.fill: parent
        opacity: 0
        Component.onCompleted: opacity = 1
        Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

        // Фон — градиент плиток: без кадра (камера выключена, показ ещё не
        // начался) окно выглядит как плитка участника, а не как дыра.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.tileFrom }
                GradientStop { position: 1.0; color: Theme.tileTo }
            }
        }

        VideoOutput {
            id: out
            anchors.fill: parent
            visible: win.videoShown
            // Демонстрацию — целиком (обрезанный слайд бесполезен), камеру —
            // во весь кадр: пропорции окна и так совпадают с кадром.
            fillMode: win.isScreen || win.full ? VideoOutput.PreserveAspectFit
                                               : VideoOutput.PreserveAspectCrop
        }

        // Камера выключена — аватарка, как на плитке.
        Column {
            visible: !win.isScreen && !win.videoShown && !win.locked
            anchors.centerIn: parent
            spacing: 10
            Avatar {
                anchors.horizontalCenter: parent.horizontalCenter
                name: win._lastName
                source: win.avatar
                size: Math.max(44, Math.min(120, Math.min(win.width, win.height) * 0.34))
            }
            Text {
                visible: win.height >= 200
                anchors.horizontalCenter: parent.horizontalCenter
                text: win.cam ? "Подключаем камеру…" : "Камера выключена"
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textXs
                font.weight: Font.Medium
            }
        }

        Text {
            visible: win.isScreen && !win.live && !win.locked && !win.ended
            anchors.centerIn: parent
            text: "Демонстрация начинается…"
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
        }

        LockPlate {
            visible: win.locked
            anchors.fill: parent
            radius: 0
            actionable: true
            onKeyRequested: win.keyRequested()
        }

        // Подсветка «говорит» — та же, что у плитки, по краю окна. Радиус —
        // как у скругления Windows 11, чтобы рамка шла ровно по краю.
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            opacity: win.speaking && !win.isScreen && !win.full ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
        }

        // ---- перетаскивание и жесты по кадру ----
        // Двигает окно системный цикл перемещения (startSystemMove): курсор,
        // плавность, переезд между мониторами и Esc — как у любого окна
        // Windows. Кнопки и ползунок у этого жеста не отнимаются:
        // TakeOverForbidden — то, что уже взяли органы управления, остаётся им.
        DragHandler {
            target: null
            grabPermissions: PointerHandler.TakeOverForbidden
            onActiveChanged: if (active && !win.full) win.startSystemMove()
        }
        TapHandler {
            gesturePolicy: TapHandler.DragThreshold
            onDoubleTapped: win.toggleFull()
            onSingleTapped: win.wake()
        }
        HoverHandler {
            id: hover
            cursorShape: win.full && win._idle ? Qt.BlankCursor : Qt.ArrowCursor
            onPointChanged: win.wake()
        }

        // ---- затемнения под органами управления ----
        // Кнопки стоят на чужом кадре любой яркости; полоса затемнения сверху
        // и снизу делает их читаемыми и на белом слайде.
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 64
            opacity: win.chromeShown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durMed } }
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.55) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0) }
            }
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 72
            opacity: win.chromeShown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durMed } }
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.6) }
            }
        }

        // ---- чип: кто это ----
        // Виден всегда (кроме полноэкранного покоя): в маленьком окне в углу
        // чужого экрана имя — единственное, что отличает одну камеру от другой.
        Rectangle {
            id: chip
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 10
            width: chipRow.implicitWidth + 18
            height: 26
            // Сколько места у чипа до кнопок справа. Считается от ширины окна,
            // а не от самого чипа: ширина чипа берётся из ширины текста, и
            // обратная зависимость замкнула бы привязку в петлю.
            readonly property real maxWidth: topRow.x - 20
            radius: 13
            color: Qt.rgba(0, 0, 0, 0.52)
            opacity: win.full && !win.chromeShown ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

            Row {
                id: chipRow
                anchors.left: parent.left
                anchors.leftMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                AppIcon {
                    id: chipIcon
                    visible: win.isScreen || !win.mic
                    anchors.verticalCenter: parent.verticalCenter
                    name: win.isScreen ? "screen" : "mic-off"
                    size: 13
                    color: win.isScreen ? Theme.accent : Theme.danger
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, Math.min(implicitWidth,
                                    chip.maxWidth - 18 - (chipIcon.visible ? 19 : 0)))
                    elide: Text.ElideRight
                    // В узком окне хватает значка экрана — слово «Демонстрация»
                    // съело бы имя, а имя тут важнее.
                    text: win.isScreen && win.width >= 360 ? "Демонстрация · " + win._lastName
                                                           : win._lastName
                    color: "#ffffff"
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                    font.weight: Font.DemiBold
                }
            }
        }

        // ---- верх справа: окно ----
        Row {
            id: topRow
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            spacing: 6
            opacity: win.chromeShown && !win.ended ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

            PipButton {
                // Узкое окно: кнопка «Открыть MeetUp» уступает место главным.
                visible: win.width >= 300
                icon: "app-window"
                tip: "Открыть MeetUp"
                onClicked: win.showMainRequested()
            }
            PipButton {
                icon: win.full ? "minimize" : "maximize"
                tip: win.full ? "Свернуть в окно" : "Во весь экран"
                onClicked: win.toggleFull()
            }
            PipButton {
                icon: "pip-return"
                tip: "Вернуть в MeetUp"
                onClicked: win.returnRequested()
            }
        }

        // ---- низ слева: свои микрофон и звук ----
        // Ради них окно и держат открытым в чужом приложении: снять микрофон,
        // не возвращаясь в MeetUp. Своя подложка — чтобы пара читалась как
        // «моё», а не как ручки этого участника.
        Rectangle {
            id: dock
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 10
            width: dockRow.implicitWidth + 8
            height: dockRow.implicitHeight + 8
            radius: height / 2
            color: Qt.rgba(0, 0, 0, 0.38)
            opacity: win.chromeShown && !win.ended ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

            Row {
                id: dockRow
                anchors.centerIn: parent
                spacing: 4
                PipButton {
                    icon: win.micOn ? "mic" : "mic-off"
                    alert: !win.micOn
                    tipAbove: true
                    tip: win.micOn ? "Выключить мой микрофон" : "Включить мой микрофон"
                    onClicked: win.micToggleRequested()
                }
                PipButton {
                    icon: win.soundOn ? "volume" : "volume-off"
                    alert: !win.soundOn
                    tipAbove: true
                    tip: win.soundOn ? "Выключить звук звонка" : "Включить звук звонка"
                    onClicked: win.soundToggleRequested()
                }
            }
        }

        // ---- низ справа: громкость этого участника / его демонстрации ----
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            spacing: 6
            layoutDirection: Qt.RightToLeft
            opacity: win.chromeShown && !win.ended ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

            PipVolume {
                id: volume
                // Звук демонстрации есть не всегда; ручка, которая ничем не
                // управляет, врала бы.
                visible: !win.isScreen || Audio.screenAudioLive
                value: win.volumeNow
                onChangeRequested: function (p) {
                    if (win.isScreen) Audio.setScreenVolume(win.pid, p)
                    else Audio.setPeerVolume(win.pid, p)
                }
            }
            PipButton {
                visible: win.isScreen && AV.devMode && win.width >= 300
                icon: "activity"
                tipAbove: true
                tip: "Сбросить буфер: перескочить на живой край"
                onClicked: win.resetRequested()
            }
        }

        // ---- прощание ----
        Rectangle {
            anchors.fill: parent
            visible: win.ended
            color: Qt.rgba(0, 0, 0, 0.72)
            Column {
                anchors.centerIn: parent
                width: parent.width - 32
                spacing: 6
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    // Без глагола прошедшего времени: по имени род не угадать,
                    // а «вышел(а)» читается как анкета.
                    text: win.isScreen ? "Демонстрация завершена"
                                       : (win._lastName || "Участник") + " — уже не в звонке"
                    color: "#ffffff"
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textSm
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: "Окно закроется само"
                    color: Qt.rgba(1, 1, 1, 0.6)
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.text2xs
                }
            }
        }
    }

    // Края — растягивание. Тянет системный цикл (startSystemResize), он же
    // держит пропорции через PipHelper. Полосы тонкие и лежат над всем, но
    // кнопки стоят в 10 px от края и с ними не пересекаются.
    component Edge: Item {
        id: edge
        property int edges: 0
        property int cursor: Qt.ArrowCursor
        visible: !win.full
        z: 100
        HoverHandler { cursorShape: edge.cursor }
        DragHandler {
            target: null
            onActiveChanged: if (active) win.startSystemResize(edge.edges)
        }
    }
    readonly property int _edgeW: 6
    Edge { anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: 12; bottomMargin: 12 }
           width: win._edgeW; edges: Qt.LeftEdge; cursor: Qt.SizeHorCursor }
    Edge { anchors { right: parent.right; top: parent.top; bottom: parent.bottom; topMargin: 12; bottomMargin: 12 }
           width: win._edgeW; edges: Qt.RightEdge; cursor: Qt.SizeHorCursor }
    Edge { anchors { top: parent.top; left: parent.left; right: parent.right; leftMargin: 12; rightMargin: 12 }
           height: win._edgeW; edges: Qt.TopEdge; cursor: Qt.SizeVerCursor }
    Edge { anchors { bottom: parent.bottom; left: parent.left; right: parent.right; leftMargin: 12; rightMargin: 12 }
           height: win._edgeW; edges: Qt.BottomEdge; cursor: Qt.SizeVerCursor }
    Edge { anchors { left: parent.left; top: parent.top } width: 12; height: 12
           edges: Qt.LeftEdge | Qt.TopEdge; cursor: Qt.SizeFDiagCursor }
    Edge { anchors { right: parent.right; bottom: parent.bottom } width: 12; height: 12
           edges: Qt.RightEdge | Qt.BottomEdge; cursor: Qt.SizeFDiagCursor }
    Edge { anchors { right: parent.right; top: parent.top } width: 12; height: 12
           edges: Qt.RightEdge | Qt.TopEdge; cursor: Qt.SizeBDiagCursor }
    Edge { anchors { left: parent.left; bottom: parent.bottom } width: 12; height: 12
           edges: Qt.LeftEdge | Qt.BottomEdge; cursor: Qt.SizeBDiagCursor }

    // Прощание длится пару секунд — достаточно, чтобы понять, куда делось
    // окно, и не настолько, чтобы закрывать его руками.
    Timer {
        running: win.ended
        interval: 2600
        onTriggered: win.returnRequested()
    }

    // ------------------------------------------------------------ Громкость
    // Зеркало, а не биндинг: Audio.peerVolume/screenVolume — методы и сами по
    // себе не пересчитываются (та же история, что у сцены демонстрации).
    property int volumeNow: 100
    Connections {
        target: Audio
        function onPeerVolumeChanged(id, percent) {
            if (!win.isScreen && id === win.pid) win.volumeNow = percent
        }
        function onScreenVolumeChanged(id, percent) {
            if (win.isScreen && id === win.pid) win.volumeNow = percent
        }
    }

    // ------------------------------------------------------- Привязка к кадрам
    // Номер привязки и id запоминаем при рождении — отцеплять строго своё,
    // как у плиток (Component.onDestruction в QML отложен).
    property int _tok: 0
    property var _pid: 0
    property bool _screen: false

    Connections {
        target: Media
        function onVideoChanged(id, active) {
            if (!win.isScreen && id === win.pid) win.live = active
        }
        function onScreenVideoChanged(id, active) {
            if (win.isScreen && id === win.pid) win.live = active
        }
        function onLockedChanged(id, isLocked) {
            if (id === win.pid) win.locked = isLocked
        }
    }

    Component.onCompleted: {
        win.place()
        PipHelper.attach(win)
        // Пока кадра нет, растягивается как 16:9 — так выглядит почти любой
        // источник; первый кадр уточнит (fitAspect).
        PipHelper.setAspect(win, 16 / 9)
        win._pid = win.pid
        win._screen = win.isScreen
        win._tok = Media.attachPip(win.pid, win.isScreen, out.videoSink)
        win.live = win.isScreen ? Media.isScreenLive(win.pid) : Media.isLive(win.pid)
        win.locked = Media.isLocked(win.pid)
        win.volumeNow = win.isScreen ? Audio.screenVolume(win.pid) : Audio.peerVolume(win.pid)
        win.visible = true
    }
    Component.onDestruction: {
        if (win._tok !== 0) Media.detachPip(win._pid, win._screen, win._tok)
        PipHelper.detach(win)
    }
}
