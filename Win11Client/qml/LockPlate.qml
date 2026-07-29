import QtQuick
import MeetUp

// Заслонка «поток зашифрован, показать нечего» — поверх плитки участника,
// закреплённой плитки и сцены демонстрации. Одна на три места намеренно: текст
// здесь объясняет ситуацию человеку, и расходиться формулировкам нельзя.
//
// Причины ровно две, и путать их нельзя — они требуют РАЗНЫХ действий:
//   ключа нет вовсе  -> «нужен ключ», человеку надо его ввести или открыть
//                       ссылку-приглашение;
//   ключ есть, но не тот -> «другой ключ», фразы у собеседников разошлись.
// Отличаем по себе, а не по собеседнику: если у нас ключа нет, то любой
// закрытый поток — это первый случай, каким бы ключом его ни закрыли.
Rectangle {
    id: root

    // На маленькой плитке в сетке подпись не помещается — остаётся замок.
    property bool compact: height < 96 || width < 132

    // Радиус задаёт тот, кого закрываем (это обычный Rectangle).
    radius: Theme.radiusLg
    // Полупрозрачная, а не глухая: под ней видно, что плитка живая, а не
    // «отвалилась». Столько же, сколько у веба (55 % фона).
    color: Qt.rgba(Theme.bg.r, Theme.bg.g, Theme.bg.b, 0.55)

    Column {
        anchors.centerIn: parent
        width: parent.width - 24
        spacing: 8

        AppIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "lock"
            size: root.compact ? 18 : 26
            color: Theme.textMuted
        }
        Text {
            visible: !root.compact
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            text: Crypto.active ? "Другой ключ" : "Зашифровано, нужен ключ"
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
            font.weight: Font.DemiBold
        }
    }
}
