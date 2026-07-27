import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import MeetUp

// Настройки конференции. Раньше это был один вертикальный свиток на 440 px:
// он не помещался в окно и мешал «выбери микрофон» с «назначь горячую клавишу».
// Теперь — рельс разделов слева и панель справа: прокрутка живёт внутри одного
// раздела, а у будущих настроек (шифрование, звук демонстрации, кодеки,
// диагностика) заранее есть свои места, и их появление не двигает вёрстку.
//
// Все значения по-прежнему пишутся в AV (MediaSettings) и применяются движками
// на лету; сами разделы лежат в отдельных файлах SettingsXxx.qml.
Item {
    id: root

    property bool open: false
    signal closed()

    anchors.fill: parent
    visible: open
    z: 200

    // Рельс и страницы строятся из одного списка: добавить раздел — дописать
    // сюда строку и положить рядом файл.
    readonly property var sections: [
        { icon: "mic",      title: "Звук",         sub: "Микрофон, динамики и качество передачи голоса", page: "SettingsAudio.qml" },
        { icon: "video",    title: "Видео",        sub: "Камера, предпросмотр и параметры трансляции",   page: "SettingsVideo.qml" },
        { icon: "screen",   title: "Демонстрация", sub: "Что видят собеседники, когда вы показываете экран", page: "SettingsShare.qml" },
        { icon: "lock",     title: "Шифрование",   sub: "Сквозное шифрование медиа и переписки",         page: "SettingsCrypto.qml" },
        { icon: "keyboard", title: "Управление",   sub: "Глобальные клавиши — работают и вне окна",      page: "SettingsKeys.qml" },
        { icon: "layout",   title: "Интерфейс",    sub: "Тема, сетка участников и уведомления",          page: "SettingsInterface.qml" },
        { icon: "activity", title: "Диагностика",  sub: "Живые показатели соединения и кодеков",         page: "SettingsStats.qml" },
        { icon: "info",     title: "О программе",  sub: "Версия, сервер и журналы",                      page: "SettingsAbout.qml" }
    ]
    property int current: 0
    // Минимальная ширина окна — 760, так что узкий случай не гипотетический:
    // рельс схлопывается в иконки, подписи уходят в подсказки.
    readonly property bool compact: width < 900

    // ---- Пункт рельса (нужен и в общем списке, и отдельно внизу) ----
    component RailItem: Item {
        id: item
        required property var info
        required property int idx
        readonly property bool active: root.current === idx

        width: parent ? parent.width : 0
        height: 40

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSm
            color: item.active ? Theme.accentSoft
                 : hh.hovered ? Theme.surface3 : "transparent"
            border.width: item.active ? 1 : 0
            border.color: Theme.accentLine
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
        }

        AppIcon {
            id: ic
            x: root.compact ? (item.width - width) / 2 : 10
            anchors.verticalCenter: parent.verticalCenter
            name: item.info.icon
            size: 18
            color: item.active ? Theme.accentInk : (hh.hovered ? Theme.text : Theme.textMuted)
        }

        Text {
            visible: !root.compact
            anchors.left: ic.right
            anchors.leftMargin: 11
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: item.info.title
            color: item.active ? Theme.accentInk : (hh.hovered ? Theme.text : Theme.textMuted)
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
            font.weight: Font.DemiBold
        }

        HoverHandler { id: hh; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.current = item.idx }

        // В узком окне подписей нет — название показывает подсказка.
        ToolTip {
            visible: root.compact && hh.hovered
            delay: 250
            x: item.width + 10
            y: (item.height - height) / 2
            contentItem: Text {
                text: item.info.title
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textXs
            }
            background: Rectangle {
                radius: Theme.radiusXs
                color: Theme.surface3
                border.width: 1
                border.color: Theme.borderStrong
            }
        }
    }

    // Затемнение: клик мимо панели закрывает.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(6 / 255, 6 / 255, 8 / 255, 0.55)
        MouseArea { anchors.fill: parent; onClicked: root.closed() }
    }

    MultiEffect {
        source: panel
        anchors.fill: panel
        shadowEnabled: true
        shadowColor: Theme.shadowColor
        shadowVerticalOffset: 12
        shadowBlur: 0.9
        autoPaddingEnabled: true
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(780, root.width - 40)
        height: Math.min(560, root.height - 40)
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        // Клики по панели не должны доходить до затемнения позади.
        MouseArea { anchors.fill: parent }

        // ---- Рельс разделов ----
        Rectangle {
            id: rail
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 1
            width: root.compact ? 60 : 212
            color: Theme.surface2
            radius: Theme.radiusCard
            Behavior on width { NumberAnimation { duration: Theme.durFast } }

            // Скругление нужно только внешним углам: правая сторона примыкает
            // к панели встык, поэтому закрываем её прямоугольной заплаткой.
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.radius
                color: parent.color
            }
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.border
            }

            Text {
                id: railTitle
                visible: !root.compact
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 20
                anchors.topMargin: 18
                text: "Настройки"
                color: Theme.text
                font.family: Theme.displayFont
                font.pixelSize: Theme.textLg
                font.weight: Font.Bold
                font.letterSpacing: -0.4
            }

            Column {
                id: railTop
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: root.compact ? 8 : 12
                anchors.rightMargin: root.compact ? 9 : 13
                anchors.topMargin: root.compact ? 16 : 52
                spacing: 3

                Repeater {
                    model: root.sections.slice(0, root.sections.length - 1)
                    delegate: RailItem {
                        required property var modelData
                        required property int index
                        info: modelData
                        idx: index
                    }
                }
            }

            // «О программе» стоит внизу и отбит чертой — это не настройка.
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: root.compact ? 8 : 12
                anchors.rightMargin: root.compact ? 9 : 13
                anchors.bottomMargin: 14
                spacing: 8

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.borderStrong
                }
                RailItem {
                    info: root.sections[root.sections.length - 1]
                    idx: root.sections.length - 1
                }
            }
        }

        // ---- Панель раздела ----
        Item {
            anchors.left: rail.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.rightMargin: 1
            anchors.topMargin: 1
            anchors.bottomMargin: 1

            Item {
                id: head
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 66

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 22
                    anchors.right: closeBtn.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3

                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: root.sections[root.current].title
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: Theme.textLg
                        font.weight: Font.Bold
                        font.letterSpacing: -0.3
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: root.sections[root.current].sub
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textXs
                    }
                }

                IconButton {
                    id: closeBtn
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    size: "sm"
                    icon: "x"
                    onClicked: root.closed()
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.border
                }
            }

            Flickable {
                id: bodyFlick
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: head.bottom
                anchors.bottom: parent.bottom
                contentWidth: width
                contentHeight: page.height + 40
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                // Полоса видна всё время, пока раздел не помещается: раньше она
                // появлялась только в движении, и обрезанный снизу текст читался
                // как баг вёрстки, а не как «здесь есть продолжение».
                ScrollBar.vertical: ScrollBar {
                    id: bodyBar
                    policy: bodyFlick.interactive ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    width: 10
                    contentItem: Rectangle {
                        implicitWidth: 4
                        radius: 2
                        color: bodyBar.pressed ? Theme.textFaint : Theme.borderStrong
                        opacity: bodyFlick.interactive ? 1 : 0
                        Behavior on color { ColorAnimation { duration: Theme.durFast } }
                    }
                    background: Item {}
                }

                Loader {
                    id: page
                    x: 22
                    y: 20
                    width: bodyFlick.width - 44
                    // Закрытая модалка не держит раздел живым: у видео там
                    // появится предпросмотр камеры, и лишний захват ни к чему.
                    active: root.open
                    source: Qt.resolvedUrl(root.sections[root.current].page)
                }
            }

            // Смена раздела прокручивает панель к началу — иначе новый раздел
            // открывается «с середины» после длинного предыдущего.
            Connections {
                target: root
                function onCurrentChanged() { bodyFlick.contentY = 0 }
            }
        }
    }
}
