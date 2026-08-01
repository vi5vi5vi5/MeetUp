import QtQuick
import MeetUp

// Раздел «О программе». Не про красоту: при разборе жалобы первым делом
// спрашивают версию и адрес сервера — пусть они будут под рукой у человека,
// а не в переписке.
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    component InfoRow: Item {
        id: line
        property string key: ""
        property string value: ""
        property bool soon: false

        width: parent ? parent.width : 0
        height: 28

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: line.key
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
        }
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            SoonChip {
                visible: line.soon
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: line.value
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
                font.weight: Font.DemiBold
            }
        }
    }

    // Шапка раздела: знак, название, версия. Без неё раздел начинался прямо со
    // строки таблицы и читался как её продолжение, а не как «о программе».
    Row {
        width: parent.width
        spacing: 16

        BrandMark {
            anchors.verticalCenter: parent.verticalCenter
            size: 56
            plate: true
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                text: "MeetUp"
                color: Theme.text
                font.family: Theme.displayFont
                font.pixelSize: 26
                font.weight: Font.ExtraBold
                font.letterSpacing: -0.9
            }
            Text {
                text: "Версия " + Qt.application.version + " · Windows"
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
            }
        }
    }

    Column {
        width: parent.width
        spacing: 2

        // «Версия клиента» отдельной строкой больше не нужна — она в шапке выше.
        InfoRow { key: "Версия сервера"; value: "—"; soon: true }
        InfoRow { key: "Сервер"; value: Sys.host }
        InfoRow { key: "Кодеки"; value: "openh264 · libvpx · opus" }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Обновление. Одна кнопка на все шаги — проверить, скачать, перезапустить:
    // отдельные кнопки на каждый шаг заставляли бы человека понимать, в каком
    // состоянии находится программа, а это не его работа.
    Field {
        width: parent.width
        label: "Обновление"
        hint: page.hintText
        AppButton {
            width: parent.width
            text: page.buttonText
            variant: (Updates.stateName === "available" || Updates.stateName === "ready")
                     ? "primary" : "ghost"
            size: "sm"
            iconRight: Updates.stateName === "available" ? "arrow-right" : ""
            icon: Updates.stateName === "ready" ? "check" : ""
            enabled: Updates.stateName !== "checking" && Updates.stateName !== "downloading"
            onClicked: page.act()
        }
    }

    Field {
        width: parent.width
        label: "Обслуживание"
        soon: true
        hint: "Журнал пока уходит в stderr — файла, который можно открыть, ещё нет."
        AppButton {
            width: parent.width
            text: "Папка с журналами"
            variant: "ghost"
            size: "sm"
            enabled: false
            opacity: 0.62
        }
    }

    readonly property string buttonText:
          Updates.stateName === "checking"    ? "Проверяем…"
        : Updates.stateName === "available"   ? "Обновить до " + Updates.latestVersion
        : Updates.stateName === "downloading" ? (Updates.progress >= 0
                                                 ? "Загрузка · " + Updates.progress + " %"
                                                 : "Загрузка…")
        : Updates.stateName === "ready"       ? "Перезапустить"
        : Updates.stateName === "failed"      ? "Попробовать снова"
        : "Проверить обновления"

    readonly property string hintText:
          Updates.stateName === "uptodate"    ? "У вас последняя версия."
        : Updates.stateName === "available"   ? "Скачается " + Updates.latestVersion
                                                + " и заменит текущую сборку."
        : Updates.stateName === "ready"       ? "Всё скачано. Программа закроется и откроется "
                                                + "заново — вернём и комнату, если вы в ней."
        : Updates.stateName === "failed"      ? Updates.errorText
        : "Проверяется при каждом запуске. Здесь — если не хочется ждать."

    function act() {
        var s = Updates.stateName
        if (s === "ready") {
            // Возврат туда, где мы сейчас: настройки открываются и из
            // конференции. Список собирает SignalingClient — там лежит и
            // пароль комнаты, которому в QML делать нечего. Пусто — значит мы
            // не в комнате, и хватит одного адреса сервера.
            var args = Conf.restartArgs(Crypto.active ? Crypto.inviteWithKey() : "")
            if (args.length === 0) args = Cli.restartToServer(Sys.serverAddress)
            Updates.installAndRestart(args)
        } else if (s === "available" || s === "failed") {
            Updates.download()
        } else {
            Updates.check()
        }
    }
}
