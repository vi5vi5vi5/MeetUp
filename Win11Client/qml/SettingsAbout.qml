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
        InfoRow { key: "Сервер"; value: Sys.host }
        // Что умеем принимать: отправка уже своя у каждой полосы и видна в
        // «Диагностике», а старая строка «openh264 · libvpx · opus» устарела в
        // день, когда демонстрация уехала на HEVC и видеокарту.
        InfoRow { key: "Кодеки"; value: "H.264 · HEVC · VP8 · VP9 · AV1 · Opus" }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Портрет сервера (GET /api/config). Здесь же, а не на отдельном экране:
    // спрашивают об этом ровно тогда же, когда о версии клиента, — при разборе
    // «почему у меня не так, как у него».
    Column {
        width: parent.width
        spacing: 2

        Text {
            width: parent.width
            bottomPadding: 8
            text: "Сервер о себе"
            color: Theme.textMuted
            font.family: Theme.labelFont
            font.pixelSize: Theme.text2xs
            font.letterSpacing: 1.4
            font.capitalization: Font.AllUppercase
        }

        InfoRow { key: "Название"; value: Server.name }
        InfoRow {
            key: "Сборка"
            value: Server.versionCommit === ""
                   ? "не сообщает"
                   : Server.versionCommit + (Server.versionModified ? " · с правками" : "")
        }
        InfoRow {
            key: "Журнал подключений"
            value: !Server.loaded ? "не сообщает"
                   : Server.logsNames ? "ведётся" : "не ведётся"
        }
        InfoRow { key: "Регистрация"; value: Server.registration ? "открыта" : "закрыта" }
        InfoRow {
            key: "Разовые комнаты"
            value: Server.anonymousRooms === "off" ? "выключены"
                   : Server.anonymousRooms === "account" ? "только из аккаунта"
                   : "создаёт кто угодно"
        }

        Text {
            width: parent.width
            topPadding: 8
            wrapMode: Text.WordWrap
            // Формулировка выбрана осознанно. Проверить эти строки снаружи
            // нечем: их сообщает тот самый сервер, о котором спрашивают, и
            // правка одной строки в его коде заставит его сказать что угодно.
            // Писать «проверено» значило бы обещать то, чего нет.
            text: "Со слов самого сервера — проверить это снаружи нечем. "
                  + "Полезно, чтобы понять, с кем имеете дело и почему часть кнопок не нарисована."
            color: Theme.textFaint
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
        }
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

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Режим разработчика. Живёт именно здесь: это не настройка звука или
    // картинки, а признание «я разбираюсь, покажите служебное» — тому же
    // разделу принадлежат версия и сервер, о которых спрашивают при разборе.
    //
    // Он не приглушает служебные настройки, а убирает их совсем: приглушённый
    // тумблер человек всё равно видит, читает и пытается понять, зачем он там.
    SettingSwitch {
        width: parent.width
        label: "Режим разработчика"
        description: AV.devMode
            ? "Показаны диагностика, выбор кодеков, буферы конвейера и ещё не подключённые тумблеры."
            : "Покажет служебное: диагностику соединения, выбор кодеков, буферы отправки. Обычной встрече это не нужно."
        checked: AV.devMode
        onToggled: function (v) { AV.devMode = v }
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
