import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import MeetUp

// Centered modal dialog over a dark scrim. Reproduces the web Modal: a titled,
// elevated card with a close button; clicking the scrim closes it. Child items
// stack in the body below the header.
Item {
    id: root

    property bool open: false
    property string title: ""
    property string subtitle: ""
    property real modalWidth: 460
    property int padding: Theme.padCard
    signal closed()

    default property alias content: body.data

    anchors.fill: parent
    visible: open
    z: 200

    // Скрим: затемнение и «щёлкнул мимо — закрыл».
    //
    // TapHandler, а не MouseArea, и без поглотителя-MouseArea на панели — это
    // не вкус, а починка. «Изменить» у имени в настройках профиля — TapHandler
    // на Text — не нажималось никогда, а кнопки рядом (AppButton на MouseArea)
    // нажимались. Механизм установлен стендом с настоящим кликом мыши:
    //  · TapHandler берёт эксклюзивный захват, и на Item/Rectangle этого
    //    хватает — доставка вниз останавливается (так живут рельс и тумблеры
    //    SettingsModal при точно таком же поглотителе);
    //  · но Text принимает левую кнопку ради ссылок, а без ссылки под курсором
    //    нажатие ОТКЛОНЯЕТ — и это отклонение снимает «принято» со всей точки,
    //    доставка идёт дальше, до MouseArea-поглотителя панели;
    //  · тот берёт захват себе, у хендлера он отменяется (CancelGrabExclusive),
    //    тап не случается.
    // Ссылки-Text с TapHandler в проекте есть и снаружи модалок (AuthScaffold,
    // HomeScreen) — там под ними нет ни одной MouseArea, и они работают; но
    // класть такой Text поверх MouseArea нельзя, а надёжнее оборачивать
    // хендлер в Item. Модалка же защищена и от такого: хендлер на скриме не
    // отбирает
    // захват ни у кого (CanTakeOverFromNothing) — ни у TapHandler, ни у
    // MouseArea кнопки, ни у Flickable тела при прокрутке. Захват достаётся ему
    // только когда под нажатием никого нет, и тогда он смотрит, где нажали и
    // отпустили: внутри панели — пустое место, ничего не делаем; снаружи —
    // закрываем.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(6 / 255, 6 / 255, 8 / 255, 0.55)
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            grabPermissions: PointerHandler.CanTakeOverFromNothing
                           | PointerHandler.ApprovesTakeOverByAnything
            onTapped: function (point) {
                var pressIn = panel.contains(panel.mapFromItem(null, point.scenePressPosition))
                var releaseIn = panel.contains(panel.mapFromItem(null, point.scenePosition))
                if (!pressIn && !releaseIn) root.closed()
            }
        }
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
        width: Math.min(root.modalWidth, root.width - 40)
        // Заголовок фиксирован, тело прокручивается: панель не выше окна,
        // а содержимое, что не влезло, доступно скроллом (как .settings-card
        // { max-height; overflow:auto } у веба).
        // Math.max: на очень низком окне вычитание уходит в минус, а элемент с
        // отрицательным размером рисуется мусором (и не восстанавливается).
        height: Math.max(0, Math.min(header.implicitHeight + bodyFlick.height
                                     + col.spacing + root.padding * 2, root.height - 40))
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        // Поглотителя кликов здесь больше нет — см. скрим выше: он сам
        // отличает нажатие по панели от нажатия мимо.

        Column {
            id: col
            x: root.padding
            y: root.padding
            width: parent.width - root.padding * 2
            spacing: 14

            // Header: title/subtitle + close button (фиксирован, не скроллится).
            Item {
                id: header
                width: parent.width
                implicitHeight: Math.max(hcol.implicitHeight, 36)
                Column {
                    id: hcol
                    width: parent.width - 44
                    spacing: 4
                    Text {
                        visible: root.title !== ""
                        text: root.title
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: Theme.textXl
                        font.weight: Font.Bold
                        font.letterSpacing: -0.5
                    }
                    Text {
                        visible: root.subtitle !== ""
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: root.subtitle
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textSm
                    }
                }
                IconButton {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    size: "sm"
                    icon: "x"
                    onClicked: root.closed()
                }
            }

            // Body slot — прокручиваемый, если выше доступной высоты.
            Flickable {
                id: bodyFlick
                width: parent.width
                height: Math.max(0, Math.min(body.implicitHeight,
                                             root.height - 40 - root.padding * 2
                                             - header.implicitHeight - col.spacing))
                contentWidth: width
                contentHeight: body.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    policy: bodyFlick.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }
                Column { id: body; width: bodyFlick.width; spacing: 14 }
            }
        }
    }
}
