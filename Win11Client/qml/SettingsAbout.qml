import QtQuick
import MeetUp

// Раздел «О программе». Не про красоту: при разборе жалобы первым делом
// спрашивают версию и адрес сервера — пусть они будут под рукой у человека,
// а не в переписке.
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    // Окно с портретом сервера открывает SettingsModal: модалка обязана
    // накрывать весь экран, а раздел живёт внутри прокрутки панели.
    signal serverInfoRequested()

    // Отклик меряем при каждом заходе в раздел: число из прошлого захода —
    // это уже не про сейчас.
    Component.onCompleted: Server.refresh()

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

        // «Версия клиента» отдельной строкой больше не нужна — она в шапке
        // выше, а адрес сервера — в карточке ниже.
        // Что умеем принимать: отправка уже своя у каждой полосы и видна в
        // «Диагностике», а старая строка «openh264 · libvpx · opus» устарела в
        // день, когда демонстрация уехала на HEVC и видеокарту.
        InfoRow { key: "Кодеки"; value: "H.264 · HEVC · VP8 · VP9 · AV1 · Opus" }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Сервер — одной карточкой, подробности по нажатию. Целиком портрет
    // сервера здесь лежать не должен: десяток строк «ключ — значение» посреди
    // настроек читается как продолжение таблицы выше, хотя это отдельный
    // разговор. В карточке — то, что спрашивают чаще всего: куда подключены и
    // жив ли он сейчас. Окно открывается то же самое, что и на экране входа.
    Column {
        width: parent.width
        spacing: 10

        Text {
            text: "Сервер"
            color: Theme.textFaint
            font.family: Theme.labelFont
            font.pixelSize: Theme.text2xs
            font.letterSpacing: 1.4
            font.capitalization: Font.AllUppercase
        }

        Rectangle {
            id: srvCard
            width: parent.width
            height: 66
            radius: Theme.radiusMd
            color: srvHover.hovered ? Theme.surface3 : Theme.surface2
            border.width: 1
            border.color: srvHover.hovered ? Theme.borderStrong : Theme.border
            Behavior on color { ColorAnimation { duration: Theme.durFast } }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 38
                    height: 38
                    radius: Theme.radiusSm
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    AppIcon {
                        anchors.centerIn: parent
                        name: "info"
                        size: 18
                        color: Theme.accentInk
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 38 - srvPing.width - 24 - 36
                    spacing: 2
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: Server.name
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textMd
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideMiddle
                        text: Sys.host
                        color: Theme.textMuted
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.textXs
                    }
                }

                Row {
                    id: srvPing
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 7; height: 7; radius: 3.5
                        color: Server.reachable ? Theme.live : Theme.danger
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Server.pingMs >= 0 ? Server.pingMs + " мс" : "—"
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textSm
                        font.weight: Font.DemiBold
                    }
                }

                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "arrow-right"
                    size: 16
                    color: srvHover.hovered ? Theme.accentInk : Theme.textFaint
                }
            }

            HoverHandler { id: srvHover; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: page.serverInfoRequested()
            }
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
