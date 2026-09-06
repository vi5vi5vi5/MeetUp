import QtQuick
import QtQuick.Effects
import QtMultimedia
import MeetUp

// Сцена демонстрации экрана: кадр целиком (никакого кропа — обрезанный слайд
// бесполезен), плашка «кто показывает» и заглушка на время, пока первый
// опорный кадр в пути. Своя демонстрация берётся из превью захвата, чужая —
// из декодера полосы SCREEN_* (Media.attachScreen).
Item {
    id: root

    property var sid: 0          // id ведущего
    property bool isSelf: false
    property string sharerName: ""
    property bool live: false    // кадры реально идут
    // Поток ведущего не открывается нашим ключом (см. LockPlate).
    property bool locked: false
    property bool expanded: false   // сцена развёрнута на весь экран

    // Развернуть/свернуть показ. Разворачивает не плитку, а весь экран — этим
    // занимается ConferenceScreen, сцена только просит.
    signal expandRequested()

    // ---- Полноэкранный показ: интерфейс уходит вместе с курсором ----
    // В развёрнутом показе смотрят чужой экран, а не наши кнопки: единственный
    // оставшийся элемент (зелёная кнопка выхода в углу) весь сеанс маячит
    // поверх чужого текста. Поэтому здесь работает правило плееров — стоит
    // мышь, нет и интерфейса; двинули её, и всё вернулось.
    //
    // Курсор гасим тем же условием и по той же причине: чужая стрелка,
    // застывшая посреди демонстрации, читается как часть картинки и сбивает с
    // толку не меньше кнопки.
    readonly property bool chromeShown: !root.expanded || !root._idle
    property bool _idle: false

    function wake() {
        root._idle = false
        if (root.expanded) idleTimer.restart()
    }
    onExpandedChanged: {
        idleTimer.stop()
        wake()          // выход из полного экрана обязан вернуть интерфейс
    }

    Timer {
        id: idleTimer
        interval: 2200
        // Пока мышь стоит НА органах управления, прятать их нельзя: человек
        // подвёл её к ручке громкости и целится — это не безделье.
        onTriggered: root._idle = !volPill.open
    }

    HoverHandler {
        id: stageHover
        // Курсор в свёрнутой сцене не наш: там своё указывают плитки и кнопки.
        cursorShape: root.chromeShown ? Qt.ArrowCursor : Qt.BlankCursor
        onPointChanged: root.wake()
    }

    Rectangle {
        id: box
        anchors.fill: parent
        radius: Theme.radiusLg
        // Фон сцены — тот же градиент, что у плиток (.stagebox у веба):
        // кадр вписывается целиком, и поля по краям должны выглядеть родными.
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.tileFrom }
            GradientStop { position: 1.0; color: Theme.tileTo }
        }

        // Маска скругления — как у VideoTile. Слой перерисовывает содержимое в
        // отдельный буфер на каждом кадре видео, и это не бесплатно; сюда её
        // вернули осознанно, потому что цена теперь ограничена с двух сторон:
        // превью своей демонстрации отдаётся не чаще 15 к/с (см. VideoEngine),
        // а чужая приходит с частотой отправителя. Ровно такой же слой висит на
        // КАЖДОЙ плитке участника — если он приемлем там, где плиток бывает
        // девять, то здесь, где сцена одна, тем более.
        Item {
            id: mediaBox
            anchors.fill: parent
            visible: root.live
            layer.enabled: true
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: boxMask
            }
            VideoOutput {
                id: out
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectFit
            }
        }

        // Маска скругления сцены (текстура для layer.effect выше).
        Item {
            id: boxMask
            anchors.fill: parent
            layer.enabled: true
            visible: false
            Rectangle { anchors.fill: parent; radius: Theme.radiusLg; color: "white" }
        }

        // Рамка — поверх видео (см. VideoTile: дети рисуются над отрисовкой
        // самого Rectangle, и собственный border ушёл бы под картинку).
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            color: "transparent"
            border.width: 1
            border.color: Theme.border
        }

        // Демонстрация зашифрована чужим ключом. Заслонка обязательна: без неё
        // сцена просто замирала на последнем открытом кадре, а потом сама
        // гасла — и вместо причины человек читал «Демонстрация начинается…»,
        // хотя она уже идёт и начаться для него не может.
        LockPlate {
            visible: root.locked
            anchors.fill: parent
        }

        // Пока кадров нет: ведущий уже закреплён, но опорный кадр ещё в пути.
        // Под заслонкой это сообщение бессмысленно — там ждать нечего.
        Text {
            visible: !root.live && !root.locked
            anchors.centerIn: parent
            text: "Демонстрация начинается…"
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
        }

        // Развернуть на весь экран. Кнопка живёт на самой демонстрации, а не в
        // шапке: разворачивать надо именно её, а окно человек и без нас
        // развернёт. Двойной щелчок по сцене делает то же — как у плееров.
        IconButton {
            id: expandBtn
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            size: "sm"
            icon: root.expanded ? "minimize" : "maximize"
            variant: root.expanded ? "active" : "neutral"
            onClicked: root.expandRequested()

            // visible, а не только opacity: прозрачная кнопка всё ещё ловила бы
            // мышь и подменяла курсор, который мы только что погасили.
            opacity: root.chromeShown ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Theme.durMed } }
        }
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onDoubleTapped: root.expandRequested()
            // Щелчок без движения — тоже признак жизни: интерфейс должен
            // вернуться, даже если мышь при этом не сдвинулась ни на пиксель.
            onSingleTapped: root.wake()
        }

        // Громкость звука демонстрации — у каждого своя. Ручка стоит здесь, а
        // не в настройках, потому что она приёмная: фонограмма одна, но одному
        // она фон, а другому мешает расслышать разговор. Пока ведущий крутил
        // общий уровень, выбор был один на всех.
        // Ползунок выползает по наведению: постоянно занимать угол кадра ради
        // ручки, которую трогают раз за встречу, — плохой размен.
        Rectangle {
            id: volPill
            anchors.right: parent.right
            anchors.top: expandBtn.bottom
            anchors.rightMargin: 12
            anchors.topMargin: 8
            // Своя демонстрация звучит у нас и так — регулировать нечего.
            // Чужая без звука тоже: ручка, которая ничем не управляет, врёт.
            visible: !root.isSelf && Audio.screenAudioLive && opacity > 0
            opacity: root.chromeShown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

            readonly property bool open: volHover.hovered || volSlider.pressed
            property int lastVolume: 100        // куда возвращаться из «тихо»

            height: 40
            width: open ? 208 : 40
            radius: height / 2
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            clip: true

            Behavior on width { NumberAnimation { duration: Theme.durMed; easing.type: Easing.OutCubic } }

            HoverHandler { id: volHover }
            // Гасит двойной щелчок по пилюле: у сцены он разворачивает показ на
            // весь экран, и подвинуть ползунок туда-обратно значило бы уехать в
            // полноэкранный режим. Лежит ПОД ползунком и кнопкой — они выше по
            // порядку и события получают первыми.
            MouseArea { anchors.fill: parent; onDoubleClicked: {} }

            Item {
                id: volIcon
                anchors.right: parent.right
                width: 40
                height: parent.height

                AppIcon {
                    anchors.centerIn: parent
                    name: AV.screenVolume === 0 ? "volume-off" : "volume"
                    size: 18
                    color: AV.screenVolume === 0 ? Theme.textMuted : Theme.text
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (AV.screenVolume > 0) {
                            volPill.lastVolume = AV.screenVolume
                            AV.screenVolume = 0
                        } else {
                            AV.screenVolume = volPill.lastVolume > 0 ? volPill.lastVolume : 100
                        }
                    }
                }
            }

            Text {
                id: volPct
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                // Ширина фиксированная: иначе ползунок дёргался бы вслед за
                // числом, пока его тянут («9%» и «195%» — разной ширины).
                width: 34
                text: AV.screenVolume + "%"
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.text2xs
                font.weight: Font.DemiBold
                opacity: volPill.open ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
            }

            PercentSlider {
                id: volSlider
                anchors.left: volPct.right
                anchors.leftMargin: 6
                anchors.right: volIcon.left
                anchors.verticalCenter: parent.verticalCenter
                enabled: volPill.open
                opacity: volPill.open ? 1 : 0
                value: AV.screenVolume
                onMoved: AV.screenVolume = Math.round(value)
                Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

                // Slider на перетаскивании присваивает value сам и тем рвёт
                // привязку выше. Дальше настройка менялась бы мимо него —
                // и «тихо» двигало бы проценты, но не ручку. Поэтому после
                // первого движения источник правды доносим руками.
                Connections {
                    target: AV
                    function onScreenVolumeChanged() { volSlider.value = AV.screenVolume }
                }
            }
        }

        // Плашка «кто показывает» — как stage-chip у веба.
        Rectangle {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 12
            width: chipRow.implicitWidth + 20
            height: chipRow.implicitHeight + 12
            radius: Theme.radiusXs
            color: Theme.scrimChip
            opacity: root.chromeShown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: 6
                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "screen"
                    size: 13
                    color: Theme.accentInk
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.isSelf
                        ? ("Вы демонстрируете " + (Screens.selectedLabel || "экран"))
                        : ("Демонстрация — " + (root.sharerName || "участник"))
                    color: Theme.dark ? "#ffffff" : Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                    font.weight: Font.Medium
                }
            }
        }
    }

    // Ведущий может смениться без пересоздания сцены (один отпустил слот,
    // другой занял), поэтому привязку переносим руками, а не в onCompleted.
    // Номер привязки — как в VideoTile: отцепляем строго своё.
    property var _boundId: 0
    property int _tok: 0
    property bool _boundSelf: false

    function bind() {
        if (root.isSelf) {
            root._tok = Media.attachScreenPreview(out.videoSink)
            root._boundSelf = true
            root.live = Qt.binding(function () { return Media.screenPreviewActive })
        } else if (root.sid) {
            root._tok = Media.attachScreen(root.sid, out.videoSink)
            root._boundId = root.sid
            root.live = Media.isScreenLive(root.sid)   // см. VideoTile
            root.locked = Media.isLocked(root.sid)     // …и по той же причине
        }
    }
    function unbind() {
        if (root._tok === 0) return
        if (root._boundSelf) Media.detachScreenPreview(root._tok)
        else if (root._boundId) Media.detachScreen(root._boundId, root._tok)
        root._boundSelf = false
        root._boundId = 0
        root._tok = 0
        root.live = false
        root.locked = false
    }

    onSidChanged: { unbind(); bind() }
    onIsSelfChanged: { unbind(); bind() }
    Component.onCompleted: bind()
    Component.onDestruction: unbind()

    Connections {
        target: Media
        function onScreenVideoChanged(id, active) {
            if (!root.isSelf && id === root.sid) root.live = active
        }
        function onLockedChanged(id, isLocked) {
            if (!root.isSelf && id === root.sid) root.locked = isLocked
        }
    }
}
