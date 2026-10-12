import QtQuick
import QtQuick.Effects
import MeetUp

// Предпросмотр ссылки под сообщением чата: картинка страницы, сайт,
// заголовок и описание. Язык MeetUp, а не чужого сайта: карточка — часть
// нашей ленты, и выглядеть она должна как соседние пузыри.
//
// Данные приходят от сервера (Previews), сам клиент на сайт не ходит — ни за
// страницей, ни за картинкой. Пока ответа нет или показывать нечего, карточки
// нет вовсе: пустая рамка «загружается» под каждой ссылкой была бы шумом.
Item {
    id: root

    property string url: ""
    property var info: ({ state: "none" })
    readonly property bool shown: info.state === "ready"
    readonly property bool hasImage: shown && info.image !== "" && pic.status !== Image.Error

    visible: shown
    implicitWidth: 300
    implicitHeight: shown ? body.height : 0

    function refresh() {
        root.info = root.url !== "" ? Previews.get(root.url) : ({ state: "none" })
    }
    onUrlChanged: refresh()
    Component.onCompleted: refresh()
    Connections {
        target: Previews
        // Ответ пришёл по какой-то ссылке; если это наша — он уже в кэше.
        function onUpdated() { if (root.info.state === "loading") root.refresh() }
    }

    // Всё содержимое — под маской скругления: картинка сверху должна
    // повторять углы карточки, а clip у Rectangle скругления не знает.
    Item {
        id: body
        width: root.width
        height: col.implicitHeight
        layer.enabled: root.shown
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
        }

        Rectangle {
            anchors.fill: parent
            color: hov.hovered ? Theme.surface3 : Theme.surface2
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
        }

        Column {
            id: col
            width: parent.width

            // Место под картинку отводится сразу, по ширине, — пузырь не
            // прыгает, когда она приезжает. Пропорция — как у og:image
            // (1200×630 почти у всех).
            Item {
                width: parent.width
                height: root.hasImage ? Math.round(width * 0.52) : 0
                visible: root.hasImage
                Rectangle { anchors.fill: parent; color: Theme.surface3 }
                Image {
                    id: pic
                    anchors.fill: parent
                    source: root.shown ? root.info.image : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    // Просим ровно размер показа: сервер отдаёт картинку как
                    // есть, а она бывает и 2000 px шириной.
                    sourceSize.width: Math.round(width * 2)
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.durMed } }
                }
            }

            Column {
                width: parent.width
                leftPadding: 12
                rightPadding: 12
                topPadding: 9
                bottomPadding: 11
                spacing: 3

                Row {
                    spacing: 6
                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "link"
                        size: 11
                        color: Theme.textFaint
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, root.width - 24 - 17)
                        elide: Text.ElideRight
                        // Всё, что пришло с чужой страницы, — только текст:
                        // заголовок с разметкой не должен стать разметкой.
                        textFormat: Text.PlainText
                        text: root.shown ? root.info.site : ""
                        color: Theme.textFaint
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.text2xs
                        font.weight: Font.DemiBold
                    }
                }
                Text {
                    visible: text !== ""
                    width: root.width - 24
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: root.shown ? root.info.title : ""
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textSm
                    font.weight: Font.Bold
                }
                Text {
                    visible: text !== ""
                    width: root.width - 24
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: root.shown ? root.info.description : ""
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                }
            }
        }
    }

    // Маска скругления (текстура для layer.effect выше).
    Item {
        id: mask
        width: body.width
        height: body.height
        layer.enabled: true
        visible: false
        Rectangle { anchors.fill: parent; radius: Theme.radiusSm; color: "white" }
    }

    // Рамка — поверх содержимого, иначе картинка закрыла бы её сверху.
    Rectangle {
        width: body.width
        height: body.height
        radius: Theme.radiusSm
        color: "transparent"
        border.width: 1
        border.color: hov.hovered ? Theme.borderStrong : Theme.border
    }

    HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
    // Открываем ссылку из сообщения, а не адрес после переадресаций: человек
    // прислал именно её.
    TapHandler {
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: Qt.openUrlExternally(root.url)
    }
}
