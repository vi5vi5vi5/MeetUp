import QtQuick
import MeetUp

// Раздел «Диагностика». Числа здесь настоящие: задержку меряет SignalingClient,
// остальное считает MediaStats на границах, где события происходят. Смысл
// раздела прикладной — следующий разговор про «звук отстаёт» начинается не с
// просьбы прислать лог, а с одного взгляда сюда.
Column {
    id: page
    width: parent ? parent.width : 0
    // Плотнее остальных разделов на пару пикселей: здесь девять блоков, и на
    // общих 18 последний уезжал под сгиб.
    spacing: 16

    property bool _copied: false
    property bool _rxFlushed: false

    // Шкала авто-сброса. Неровная намеренно: внизу важны десятые доли секунды
    // (0,3 против 0,7 — разное поведение на тяжёлом опорном кадре), вверху —
    // целые. Крайняя позиция — «выключен», ноль в настройке.
    readonly property var resetSteps: [
        { value: 300,   label: "0,3" },
        { value: 500,   label: "0,5" },
        { value: 700,   label: "0,7" },
        { value: 1000,  label: "1 с" },
        { value: 1500,  label: "1,5" },
        { value: 2000,  label: "2" },
        { value: 3000,  label: "3" },
        { value: 5000,  label: "5" },
        { value: 7000,  label: "7" },
        { value: 10000, label: "10 с" },
        { value: 0,     label: "Выкл" }
    ]
    // «0,3 с», «1 с», «1,5 с» — как принято писать по-русски, с запятой.
    function fmtSeconds(ms) {
        var s = ms / 1000
        return (Number.isInteger(s) ? String(s) : s.toFixed(1).replace(".", ",")) + " с"
    }

    // «1 поток · 2 потока · 5 потоков». Русские числительные — та мелочь,
    // по которой видно, писали интерфейс или переводили.
    function plural(n, one, few, many) {
        const m10 = n % 10, m100 = n % 100
        if (m10 === 1 && m100 !== 11) return one
        if (m10 >= 2 && m10 <= 4 && (m100 < 10 || m100 >= 20)) return few
        return many
    }

    // Сколько миллисекунд отведено на кадр при такой частоте. Число само по
    // себе бесполезно, а рядом с ценой кадра отвечает на весь вопрос: успевает
    // машина или нет.
    function frameBudget(fps) {
        return fps > 0 ? (1000 / fps).toFixed(1) : "—"
    }

    component Metric: Rectangle {
        id: cell
        property string caption: ""
        property string value: "—"
        property string unit: ""
        property string sub: ""
        // Показатель не про эту минуту (камера выключена, никто не вещает):
        // гасим, но не прячем — пустая клетка объясняет не меньше полной.
        property bool idle: false

        height: 64
        radius: Theme.radiusMd
        color: Theme.surface2
        border.width: 1
        border.color: Theme.border
        opacity: idle ? 0.62 : 1

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Text {
                text: cell.caption.toUpperCase()
                color: Theme.textFaint
                font.family: Theme.labelFont
                font.pixelSize: 10
                font.letterSpacing: 1.4
                font.weight: Font.Medium
            }
            Row {
                spacing: 5
                Text {
                    id: valueText
                    text: cell.value
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLg
                    font.weight: Font.Bold
                }
                Text {
                    anchors.baseline: valueText.baseline
                    visible: cell.unit !== ""
                    text: cell.unit
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                    font.weight: Font.DemiBold
                }
            }
            Text {
                width: parent.width
                visible: cell.sub !== ""
                text: cell.sub
                elide: Text.ElideRight
                color: Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.text2xs
            }
        }
    }

    Rectangle {
        width: parent.width
        height: 46
        radius: Theme.radiusMd
        color: Theme.surface2
        border.width: 1
        border.color: Theme.border

        Rectangle {
            id: lamp
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: Conf.ping < 0 ? Theme.textFaint
                 : Conf.ping < 200 ? Theme.accent : Theme.danger
        }
        Text {
            anchors.left: lamp.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: (Conf.ping < 0 ? "Соединение проверяется"
                 : Conf.ping < 200 ? "Соединение устойчиво" : "Связь неровная")
                 + " · " + Sys.host
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
        }
    }

    Grid {
        width: parent.width
        columns: 2
        columnSpacing: 10
        rowSpacing: 10

        Metric {
            width: (parent.width - 10) / 2
            caption: "Задержка"
            value: Conf.ping < 0 ? "—" : String(Conf.ping)
            unit: Conf.ping < 0 ? "" : "мс"
            sub: "круг до сервера и обратно"
            idle: Conf.ping < 0
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Приём"
            value: String(Stats.rxKbps)
            unit: "кбит/с"
            sub: Stats.rxStreams === 0 ? "видео никто не шлёт"
               : Stats.rxFps + " к/с · " + Stats.rxStreams + " "
                 + page.plural(Stats.rxStreams, "поток", "потока", "потоков")
            idle: Stats.rxKbps === 0
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Отправка · голос"
            value: String(Stats.txVoiceKbps)
            unit: "кбит/с"
            sub: Stats.txVoiceKbps > 0 ? "микрофон в эфире" : "микрофон молчит"
            idle: Stats.txVoiceKbps === 0
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Отправка · камера"
            value: String(Stats.txCamKbps)
            unit: "кбит/с"
            sub: Stats.txCamFormat === "" ? "камера выключена"
               : Stats.txCamFormat + " · " + Stats.txCamFps + " к/с"
            idle: Stats.txCamFormat === ""
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Отправка · экран"
            value: String(Stats.txScrKbps)
            unit: "кбит/с"
            sub: Stats.txScrFormat === "" ? "демонстрации нет"
               : Stats.txScrFormat + " · " + Stats.txScrFps + " к/с"
                 + (Stats.txScrAudio ? " · со звуком" : "")
            idle: Stats.txScrFormat === ""
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Подстройка под звук"
            value: String(Stats.syncHoldMs)
            unit: "мс"
            sub: Stats.syncHoldMs > 0 ? "картинка ждёт свой звук" : "видео идёт без задержки"
            idle: Stats.syncHoldMs === 0
        }
        // Эхоподавитель. Задержка — главное число: столько миллисекунд звук
        // идёт из синка через колонки обратно в микрофон, и фильтр обязан её
        // найти. Динамики звучат, а задержки нет — значит, не нашёл
        // (см. src/media/EchoCanceller.h про выравнивание).
        Metric {
            width: (parent.width - 10) / 2
            caption: "Эхоподавление"
            value: Stats.aecOn ? Stats.aecSuppressionDb.toFixed(0) : "—"
            unit: Stats.aecOn ? "дБ" : ""
            // Дрейф — только когда он есть: на общем клоке это ноль, и слово
            // «дрейф» там лишь пугало бы.
            sub: !Stats.aecOn ? (AV.echoCancel ? "микрофон выключен" : "выключено в «Звуке»")
               : (Stats.aecDelayMs >= 0 ? "эхо через " + Stats.aecDelayMs + " мс"
                  : Stats.aecFarActive ? "ищет задержку эха…" : "динамики молчат")
                 + (Stats.aecDriftPpm !== 0 ? " · дрейф " + Stats.aecDriftPpm + " ppm" : "")
            idle: !Stats.aecOn
        }
        // Цена кадра. Смотреть сюда надо, когда картинка рвётся, а канал при
        // этом свободен: в бюджет кадра (16.7 мс при 60 к/с, 33 при 30) обязаны
        // укладываться оба числа, и пик — в первую очередь. Не укладывается —
        // виноват не интернет, а машина.
        Metric {
            width: (parent.width - 10) / 2
            caption: "Кодирование · экран"
            value: Stats.txScrFormat === "" ? "—" : Stats.txScrEncodeMs.toFixed(1)
            unit: Stats.txScrFormat === "" ? "" : "мс"
            sub: Stats.txScrFormat === "" ? "демонстрации нет"
               : "пик " + Stats.txScrEncodePeakMs.toFixed(1) + " мс · бюджет "
                 + page.frameBudget(Stats.txScrFps) + " мс"
            idle: Stats.txScrFormat === ""
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Разбор · экран"
            value: Stats.rxScrDecoder === "" ? "—" : Stats.rxScrDecodeMs.toFixed(1)
            unit: Stats.rxScrDecoder === "" ? "" : "мс"
            sub: Stats.rxScrDecoder === "" ? "чужой демонстрации нет"
               : Stats.rxScrDecoder + " · пик "
                 + Stats.rxScrDecodePeakMs.toFixed(1) + " мс"
            idle: Stats.rxScrDecoder === ""
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Кодирование · камера"
            value: Stats.txCamFormat === "" ? "—" : Stats.txCamEncodeMs.toFixed(1)
            unit: Stats.txCamFormat === "" ? "" : "мс"
            sub: Stats.txCamFormat === "" ? "камера выключена"
               : "бюджет " + page.frameBudget(Stats.txCamFps) + " мс"
            idle: Stats.txCamFormat === ""
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Разбор · камеры"
            value: Stats.rxCamDecoder === "" ? "—" : Stats.rxCamDecodeMs.toFixed(1)
            unit: Stats.rxCamDecoder === "" ? "" : "мс"
            sub: Stats.rxCamDecoder === "" ? "камер участников не видно"
                                           : Stats.rxCamDecoder
            idle: Stats.rxCamDecoder === ""
        }
    }

    // Пропуски отдельной строкой, а не клеткой: это не «сколько», а «почему»,
    // и читать их надо вместе с полосами выше. «Потерь пакетов» здесь нет и не
    // будет: транспорт — TCP, по дороге не теряется ничего. Кадры теряем мы
    // сами, до отправки, — когда сокет забит или кодировщик не поспевает.
    Field {
        id: dropField
        width: parent.width
        label: "Пропуск кадров"
        // Одного процента мало, и это выяснилось дорого: «канал не тянет» и
        // «кодировщик не поспевает» выглядели в нём одинаково, а лечатся
        // противоположным — первое разрешением и битрейтом, второе кодеком и
        // машиной. Поэтому под плитками стоит разбор по причинам, и подпись
        // называет ту, которой больше.
        readonly property int scrBusy: Stats.scrDropBusyPercent
        readonly property int camBusy: Stats.camDropBusyPercent
        readonly property int scrJam: Stats.scrDropCongestionPercent
        readonly property int camJam: Stats.camDropCongestionPercent
        hint: (Stats.txCamFormat === "" && Stats.txScrFormat === "")
              ? "Считается, пока вы вещаете: кадры, снятые, но не ушедшие в сеть."
              : (scrBusy + camBusy) > (scrJam + camJam)
                ? "Не поспевает кодировщик: кадр снят, а предыдущий ещё не закодирован. Лечится кодеком полегче, разрешением или частотой пониже."
                : "Забит канал: кадр снят, но в сокете уже больше, чем он успевает отдать. Лечится битрейтом и разрешением."

        // Своя Column: Field складывает содержимое в Item, не в колонку.
        Column {
            width: parent.width
            spacing: 8

            Row {
                width: parent.width
                spacing: 8

                Rectangle {
                    width: (parent.width - 8) / 2
                    height: 34
                    radius: Theme.radiusSm
                    color: Theme.surface2
                    border.width: 1
                    border.color: Stats.camDropPercent > 20 ? Theme.danger : Theme.border
                    Text {
                        anchors.centerIn: parent
                        text: "Камера · " + Stats.camDropPercent + "%"
                        color: Stats.txCamFormat === "" ? Theme.textFaint : Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textXs
                        font.weight: Font.Medium
                    }
                }
                Rectangle {
                    width: (parent.width - 8) / 2
                    height: 34
                    radius: Theme.radiusSm
                    color: Theme.surface2
                    border.width: 1
                    border.color: Stats.scrDropPercent > 20 ? Theme.danger : Theme.border
                    Text {
                        anchors.centerIn: parent
                        text: "Экран · " + Stats.scrDropPercent + "%"
                        color: Stats.txScrFormat === "" ? Theme.textFaint : Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textXs
                        font.weight: Font.Medium
                    }
                }
            }

            Text {
                width: parent.width
                visible: Stats.camDropPercent > 0 || Stats.scrDropPercent > 0
                wrapMode: Text.WordWrap
                text: "Камера: канал " + dropField.camJam + "%, кодировщик "
                      + dropField.camBusy + "%  ·  экран: канал "
                      + dropField.scrJam + "%, кодировщик "
                      + dropField.scrBusy + "%"
                color: Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.text2xs
            }
        }
    }

    // Очередь приёма. Это единственное место, где видно, что декодер отстаёт:
    // байты идут, кадры приходят, а показывается прошлое — потому что всё
    // принятое стоит здесь и ждёт своей очереди на разбор.
    //
    // Выключателя у буфера нет намеренно (был): без него картинка рвалась
    // всегда, даже когда в очереди набирался один кадр. Единственная ручка —
    // порог авто-сброса ниже; кнопка — то же самое, но прямо сейчас.
    Field {
        id: rxField
        width: parent.width
        label: "Буфер приёма"
        hint: "Кадр, который декодер не успел разобрать, ждёт своей очереди. Пока он поспевает, здесь ноль; растущее число — отставание, которое само не рассосётся."
              + (Stats.rxResets > 0 ? " Сбросов за сеанс: " + Stats.rxResets + "." : "")

        // Красным подсвечивается то, что вот-вот сбросится (или сбросилось бы,
        // будь авто-сброс включён): порог тот же, что у него.
        readonly property int hotMs: AV.rxAutoResetMs > 0 ? AV.rxAutoResetMs : 1000

        // Клетка очереди: сколько лежит и сколько это в миллисекундах по полосам.
        component QueueCell: Rectangle {
            property string text: ""
            property bool hot: false
            property bool live: false
            height: 34
            radius: Theme.radiusSm
            color: Theme.surface2
            border.width: 1
            border.color: hot ? Theme.danger : Theme.border
            Text {
                anchors.centerIn: parent
                text: parent.text
                color: parent.live ? Theme.text : Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.textXs
                font.weight: Font.Medium
            }
        }

        // Field кладёт содержимое в Item, а не в колонку: без своей Column
        // клетки и кнопка легли бы друг на друга.
        Column {
            width: parent.width
            spacing: 8

            Row {
                width: parent.width
                spacing: 8
                QueueCell {
                    width: (parent.width - 16) / 3
                    text: "В очереди · " + Stats.rxQueue
                    live: Stats.rxQueue > 0
                    // Очередь длиннее полусекунды на любой разумной частоте —
                    // это уже видимое опоздание, а не рабочий запас.
                    hot: Stats.rxQueue > 15
                }
                QueueCell {
                    width: (parent.width - 16) / 3
                    text: "Камеры · " + Stats.rxLagCamMs + " мс"
                    live: Stats.rxLagCamMs > 0
                    hot: Stats.rxLagCamMs > rxField.hotMs
                }
                QueueCell {
                    width: (parent.width - 16) / 3
                    text: "Экран · " + Stats.rxLagScrMs + " мс"
                    live: Stats.rxLagScrMs > 0
                    hot: Stats.rxLagScrMs > rxField.hotMs
                }
            }
            AppButton {
                width: parent.width
                text: page._rxFlushed ? "Сброшено — ждём опорный кадр" : "Сбросить буфер сейчас"
                variant: page._rxFlushed ? "primary" : "secondary"
                size: "sm"
                onClicked: { Media.flushReceive(); page._rxFlushed = true; rxFlushed.restart() }
            }
        }
        Timer { id: rxFlushed; interval: 1600; onTriggered: page._rxFlushed = false }
    }

    // Порог авто-сброса. Ползунок, а не число: правильного значения нет — оно
    // зависит от кодека, машины и того, что человек смотрит. Показ слайдов
    // терпит и пять секунд, разговор — не терпит и одной.
    Field {
        width: parent.width
        label: "Авто-сброс при отставании · "
               + (AV.rxAutoResetMs === 0 ? "выключен" : page.fmtSeconds(AV.rxAutoResetMs))
        hint: AV.rxAutoResetMs === 0
            ? "Очередь копится без предела: отставание видно в клетках выше и уходит только кнопкой. Так стоит жить, если рывки хуже опоздания — например, на показе слайдов без разговора."
            : "Кадр пролежал в очереди дольше порога — вся очередь выбрасывается, картинка перескакивает на живой край и ждёт опорный кадр. Ниже полусекунды сбрасывать будет и один тяжёлый опорный кадр демонстрации; выше двух секунд отставание уже мешает разговору."
        StepSlider {
            width: parent.width
            model: page.resetSteps
            value: AV.rxAutoResetMs
            offLast: true
            onPicked: function (v) { AV.rxAutoResetMs = v }
        }
    }

    Field {
        width: parent.width
        label: "Отчёт для разбора"
        hint: "Всё с этого экрана одним текстом — вместо просьбы «пришлите лог»."

        AppButton {
            width: parent.width
            text: page._copied ? "Скопировано" : "Скопировать отчёт"
            variant: page._copied ? "primary" : "secondary"
            size: "sm"
            onClicked: {
                Sys.copyText(page.report())
                page._copied = true
                copyReset.restart()
            }
        }
    }

    Timer { id: copyReset; interval: 1400; onTriggered: page._copied = false }

    // Снимок состояния текстом. Порядок как в разговоре о проблеме: кто я,
    // куда подключён, что принимаю, что отправляю, чем настроено.
    function report() {
        const lines = []
        lines.push("MeetUp " + Qt.application.version + " · Windows")
        lines.push("Сервер: " + Sys.host
                   + " · задержка " + (Conf.ping < 0 ? "—" : Conf.ping + " мс"))
        lines.push("Приём: " + Stats.rxKbps + " кбит/с · " + Stats.rxFps + " к/с · "
                   + Stats.rxStreams + " "
                   + page.plural(Stats.rxStreams, "видеопоток", "видеопотока", "видеопотоков"))
        lines.push("Голос: " + Stats.txVoiceKbps + " кбит/с")
        // Пропуск идёт с разбором по причине: без него читающий отчёт начинает
        // с угадывания, канал виноват или машина.
        lines.push("Камера: " + (Stats.txCamFormat === "" ? "выключена"
                   : Stats.txCamFormat + " · " + Stats.txCamFps + " к/с · "
                     + Stats.txCamKbps + " кбит/с · пропуск " + Stats.camDropPercent
                     + "% (канал " + Stats.camDropCongestionPercent
                     + "%, кодировщик " + Stats.camDropBusyPercent + "%)"
                     + " · кодирование " + Stats.txCamEncodeMs.toFixed(1) + " мс"
                     + " при бюджете " + page.frameBudget(Stats.txCamFps) + " мс"))
        lines.push("Демонстрация: " + (Stats.txScrFormat === "" ? "нет"
                   : Stats.txScrFormat + " · " + Stats.txScrFps + " к/с · "
                     + Stats.txScrKbps + " кбит/с"
                     + (Stats.txScrAudio ? " (со звуком)" : "")
                     + " · пропуск " + Stats.scrDropPercent
                     + "% (канал " + Stats.scrDropCongestionPercent
                     + "%, кодировщик " + Stats.scrDropBusyPercent + "%)"
                     + " · кодирование " + Stats.txScrEncodeMs.toFixed(1) + " мс"
                     + " (пик " + Stats.txScrEncodePeakMs.toFixed(1) + ")"
                     + " при бюджете " + page.frameBudget(Stats.txScrFps) + " мс"))
        // Приёмная сторона. Строка декодера здесь важнее прочего: программный
        // разбор — самая частая причина рваной чужой картинки при живом канале.
        lines.push("Разбор чужой демонстрации: " + (Stats.rxScrDecoder === "" ? "нет"
                   : Stats.rxScrDecoder + " · " + Stats.rxScrDecodeMs.toFixed(1) + " мс"
                     + " (пик " + Stats.rxScrDecodePeakMs.toFixed(1) + ")"))
        lines.push("Разбор камер участников: " + (Stats.rxCamDecoder === "" ? "нет"
                   : Stats.rxCamDecoder + " · " + Stats.rxCamDecodeMs.toFixed(1) + " мс"))
        lines.push("Подстройка под звук: " + Stats.syncHoldMs + " мс")
        lines.push("Очередь приёма: " + Stats.rxQueue + " кадров · отставание камеры "
                   + Stats.rxLagCamMs + " мс, экран " + Stats.rxLagScrMs + " мс"
                   + " · сбросов " + Stats.rxResets + " · авто-сброс "
                   + (AV.rxAutoResetMs === 0 ? "выкл" : page.fmtSeconds(AV.rxAutoResetMs)))
        lines.push("Настройки: камера " + AV.camQuality
                   + ", экран " + (AV.screenRes === "src" ? "источник" : AV.screenRes + "p")
                   + "/" + AV.screenFps + "к/с"
                   + "/" + (AV.screenBitrate === "auto" ? "битрейт авто"
                                                        : (AV.screenBitrate / 1000) + " Мбит/с")
                   + ", звук демонстрации " + (AV.screenAudio ? "вкл" : "выкл")
                   + ", громкость демонстрации " + AV.screenVolume + "%")
        return lines.join("\n")
    }
}
