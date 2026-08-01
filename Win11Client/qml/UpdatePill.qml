import QtQuick
import MeetUp

// Пилюля обновления в шапке главной, рядом с логотипом. Знак не подменяет и не
// прячет: фирменный знак — часть узнавания, а его исчезновение читается как
// «что-то сломалось». Появляется, только когда есть о чём сказать, поэтому в
// обычной жизни шапка выглядит ровно как раньше.
//
// Одна кнопка на все шаги: «Обновить» -> «Загрузка 40 %» -> «Перезапустить».
// Отдельного окна нет намеренно — обновление здесь не событие, требующее
// внимания, а строчка, которую замечают между делом.
Rectangle {
    id: pill

    // Что передать перезапущенному экземпляру: сервер, на котором мы сейчас.
    // Комнату не передаём — пилюля видна только на главной, а туда попадают,
    // выйдя из конференции. Обновиться, не выходя из разговора, можно из
    // настроек: там кнопка знает про комнату (см. SettingsAbout).
    property var restartArgs: Cli.restartToServer(Sys.serverAddress)

    readonly property string st: Updates.stateName
    readonly property bool busy: st === "downloading"

    visible: st === "available" || st === "downloading" || st === "ready" || st === "failed"
    width: visible ? row.implicitWidth + 22 : 0
    height: 30
    radius: Theme.radiusPill
    color: st === "ready" ? Theme.accent
         : hover.hovered && !busy ? Theme.surface2
         : Theme.surface
    border.width: 1
    border.color: st === "failed" ? Theme.danger
                : st === "ready" ? Theme.accent
                : Theme.borderStrong

    Behavior on width { NumberAnimation { duration: Theme.durMed; easing.type: Easing.OutQuad } }

    readonly property color ink: st === "ready" ? Theme.accentFg
                               : st === "failed" ? Theme.danger
                               : Theme.text

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7

        AppIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: !pill.busy
            name: pill.st === "ready" ? "check" : pill.st === "failed" ? "info" : "arrow-right"
            size: 14
            color: pill.ink
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: pill.st === "available"   ? "Версия " + Updates.latestVersion + " · Обновить"
                : pill.st === "downloading" ? (Updates.progress >= 0
                                               ? "Загрузка · " + Updates.progress + " %"
                                               : "Загрузка…")
                : pill.st === "ready"       ? "Перезапустить"
                : "Не удалось обновить"
            color: pill.ink
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
            font.weight: Font.DemiBold
        }
    }

    HoverHandler {
        id: hover
        enabled: !pill.busy
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        enabled: !pill.busy
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            if (pill.st === "ready") Updates.installAndRestart(pill.restartArgs)
            else Updates.download()      // из «доступно» и из «не вышло» — одинаково
        }
    }

    // Почему не вышло — под пилюлей, а не в модальном окне: чаще всего это
    // «нет прав на запись», и человеку нужен текст, а не кнопка «ОК».
    Text {
        visible: pill.st === "failed" && Updates.errorText !== ""
        anchors.top: parent.bottom
        anchors.topMargin: 6
        anchors.left: parent.left
        width: 320
        wrapMode: Text.WordWrap
        text: Updates.errorText
        color: Theme.textFaint
        font.family: Theme.uiFont
        font.pixelSize: Theme.text2xs
    }
}
