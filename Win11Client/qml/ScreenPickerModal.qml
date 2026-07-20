import QtQuick
import MeetUp

// Выбор объекта демонстрации (M7). В браузере этот диалог рисует сам браузер;
// у настольного клиента такого диалога нет, поэтому выбор — часть интерфейса:
// мониторы карточками с живыми миниатюрами, окна — списком.
// Подтверждение уходит в Conf.setScreenShare(true); кадры пойдут только после
// того, как сервер закрепит слот за нами (§4.3).
AppModal {
    id: root
    title: "Демонстрация экрана"
    subtitle: "Выберите, что увидят участники."
    modalWidth: 560

    property int tab: 0                      // 0 — экраны, 1 — окна
    property string pick: ""                 // ключ выбранного источника

    signal confirmed()

    // Открытие: пересобрать списки (окна открылись/закрылись, кадр устарел) и
    // предложить то, что выбирали в прошлый раз, иначе первый монитор.
    onOpenChanged: {
        if (!open) return
        Screens.refresh()
        tab = 0
        // Открываемся всегда на «Экране», поэтому и подставленный выбор должен
        // быть экраном — иначе кнопка «Показать» отправила бы запомненное окно,
        // которого на виду нет.
        pick = Screens.selectedKey.indexOf("screen:") === 0 ? Screens.selectedKey
             : (Screens.screens.length > 0 ? Screens.screens[0].key : "")
    }

    // Переключили вкладку — выбор не должен остаться на невидимой карточке,
    // иначе кнопка «Показать» отправила бы совсем не то, что видно на экране.
    // Заодно доснимаем предпросмотр окон: он дорогой, и платить за него, когда
    // человек показывает монитор, незачем.
    onTabChanged: {
        if (tab === 1) Screens.refreshWindowThumbs()
        if (pick.indexOf(tab === 0 ? "screen:" : "window:") === 0) return
        pick = (tab === 0 && Screens.screens.length > 0) ? Screens.screens[0].key : ""
    }

    // ---- Переключатель «Экран / Окно» ----
    Row {
        spacing: 8
        Repeater {
            model: [ { label: "Экран", idx: 0 }, { label: "Окно", idx: 1 } ]
            delegate: Rectangle {
                required property var modelData
                width: swLabel.implicitWidth + 28
                height: 34
                radius: Theme.radiusPill
                color: root.tab === modelData.idx ? Theme.accent : Theme.surface2
                Text {
                    id: swLabel
                    anchors.centerIn: parent
                    text: modelData.label
                    color: root.tab === modelData.idx ? Theme.accentFg : Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textSm
                    font.weight: Font.Bold
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.tab = modelData.idx }
            }
        }
    }

    // ---- Карточки источников: снимок, название, размер ----
    // Мониторы и окна показываются одинаково: у окна такой же предпросмотр,
    // снятый через PrintWindow (см. ScreenSources).
    Flow {
        id: cards
        width: parent.width
        spacing: Theme.gapGrid

        Repeater {
            model: root.tab === 0 ? Screens.screens : Screens.windows
            delegate: Rectangle {
                required property var modelData
                readonly property bool chosen: root.pick === modelData.key
                // Два в ряд; на узком окне модалка ужимается — считаем от Flow.
                width: (cards.width - Theme.gapGrid) / 2
                height: width * 9 / 16 + 46
                radius: Theme.radiusMd
                color: chosen ? Theme.surface3 : Theme.surface2
                border.width: chosen ? 2 : 1
                border.color: chosen ? Theme.accent : Theme.border

                Column {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    Rectangle {
                        width: parent.width
                        height: parent.width * 9 / 16
                        radius: Theme.radiusXs
                        clip: true
                        color: Theme.dark ? "#0a0a0c" : "#dedcd2"

                        // Снимок. Адрес несёт nonce, поэтому после «Обновить»
                        // QML перезапрашивает картинку, а не берёт из кэша.
                        Image {
                            id: shot
                            anchors.fill: parent
                            anchors.margins: 1
                            source: modelData.thumb
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                        }
                        // Снимок не удался (защищённое окно и т.п.) — значок.
                        AppIcon {
                            visible: shot.status !== Image.Ready
                            anchors.centerIn: parent
                            name: "screen"
                            size: 22
                            color: Theme.textFaint
                        }
                    }
                    Text {
                        width: parent.width
                        text: modelData.label
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textXs
                        font.weight: Font.Medium
                    }
                    Text {
                        width: parent.width
                        text: modelData.sub
                        elide: Text.ElideRight
                        color: Theme.textFaint
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.text2xs
                    }
                }

                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.pick = modelData.key }
            }
        }
    }

    Text {
        visible: root.tab === 1 && Screens.windows.length === 0
        width: parent.width
        wrapMode: Text.Wrap
        text: "Подходящих окон не нашлось: в списке только развёрнутые окна приложений — "
            + "свёрнутые и служебные окна системы захватить нельзя. Разверните нужное и нажмите «Обновить»."
        color: Theme.textMuted
        font.family: Theme.uiFont
        font.pixelSize: Theme.textSm
    }

    // ---- Подвал ----
    Item {
        width: parent.width
        height: 44

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - refreshBtn.width - startBtn.width - 24
            elide: Text.ElideRight
            text: "Звук системы не передаётся — только изображение."
            color: Theme.textFaint
            font.family: Theme.uiFont
            font.pixelSize: Theme.text2xs
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            AppButton {
                id: refreshBtn
                text: "Обновить"
                variant: "secondary"
                size: "sm"
                onClicked: {
                    Screens.refresh()
                    if (root.tab === 1) Screens.refreshWindowThumbs()
                }
            }
            AppButton {
                id: startBtn
                text: "Показать"
                variant: "primary"
                size: "sm"
                enabled: root.pick !== ""
                onClicked: {
                    Screens.select(root.pick)
                    // Источник мог исчезнуть, пока модалка была открыта
                    // (окно закрыли): тогда показываем свежий список.
                    if (Screens.selectedKey === root.pick) root.confirmed()
                    else Screens.refresh()
                }
            }
        }
    }
}
