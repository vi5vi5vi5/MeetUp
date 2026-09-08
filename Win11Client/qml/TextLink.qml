import QtQuick
import MeetUp

// Текстовая ссылка: «Изменить», «Показать», «Очистить», «Войти». Text лежит
// ВНУТРИ Item, а хендлеры висят на Item — и это не для порядка.
//
// Text принимает левую кнопку мыши ради ссылок в разметке, а нажатие без
// ссылки под курсором ОТКЛОНЯЕТ. Отклонение снимает «принято» со всей точки,
// и доставка идёт дальше вниз, к следующему элементу под курсором. Внутри
// модалки это её поглотитель кликов: он берёт захват себе, у TapHandler захват
// отменяется, тап не случается — так «Изменить» у имени не нажималось никогда.
// А без поглотителя нажатие уходило прямо на страницу под модалкой — так клик
// по «Изменить» однажды нажал «Завершить» на карточке комнаты под ней.
//
// У Item собственной обработки мыши нет: хендлер берёт захват, и доставка на
// нём останавливается — до поглотителя не доходит ни одного нажатия (доказано
// стендом с настоящим кликом). Отсюда правило: TapHandler прямо на Text в
// проекте не вешать, только через этот компонент.
Item {
    id: root

    property string text: ""
    property color color: Theme.accentInk
    // Цвет при наведении. По умолчанию тот же — подсветка нужна не всем
    // ссылкам (у «ВОЙТИ» в шапке она есть, у «Изменить» — нет).
    property color hoverColor: color
    property alias font: label.font
    readonly property bool hovered: hh.hovered
    signal clicked()

    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight

    Text {
        id: label
        anchors.fill: parent
        text: root.text
        color: root.hovered ? root.hoverColor : root.color
        font.family: Theme.uiFont
        font.pixelSize: Theme.textXs
        font.weight: Font.DemiBold
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    HoverHandler { id: hh; cursorShape: Qt.PointingHandCursor }
    TapHandler {
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: root.clicked()
    }
}
