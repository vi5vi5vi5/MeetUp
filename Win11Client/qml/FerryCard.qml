import QtQuick
import MeetUp

// Карточка Ferry под сообщением чата — «груз к выдаче», приём, «доставлено»
// и все промежуточные состояния (доски «Виджет», «Состояния», «В ленте»).
// Сама ничего не делает: всё — у раздачи (Ferry.transfer, src/ferry), одной
// на ссылку, сколько бы раз эта ссылка ни встретилась в ленте.
Item {
    id: root

    property string url: ""
    property string author: ""
    property bool self: false
    // «Попросить в чате»: вставить просьбу в поле ввода — отправит человек.
    signal ask(string text)

    readonly property var t: url !== "" ? Ferry.transfer(url) : null
    readonly property string phase: t ? t.phase : ""

    visible: t !== null
    implicitHeight: !t ? 0 : t.declined ? collapsed.height : card.height

    function esc(s) {
        return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }
    readonly property string who: root.self ? "Вы" : (root.author !== "" ? root.author : "Отправитель")
    readonly property real progress: t && t.total > 0 ? t.bytes / t.total : (phase === "done" ? 1 : 0)
    readonly property bool busy: ["connecting", "receiving", "reconnecting"].indexOf(phase) >= 0

    readonly property string headLabel: {
        switch (phase) {
        case "fog": return "в тумане"
        case "password": case "unlocking": return "под пломбой"
        case "nokey": return "ключ не предъявлен"
        case "approval": return "у двери"
        case "connecting": case "receiving": case "reconnecting": case "stopped": return "приём"
        case "done": return "отметка о доставке"
        case "failed": return t && t.errorCode === "sender_gone" ? "причал пуст" : "отказ в перевозке"
        default: return "груз к выдаче"
        }
    }
    readonly property string chip: {
        switch (phase) {
        case "connecting": return "связь"
        case "receiving": return t && t.senderAway ? "пауза" : "идёт"
        case "reconnecting": return "пауза"
        case "stopped": return "стоп"
        case "approval": return "ждём"
        default: return ""
        }
    }
    readonly property string sceneMode: {
        switch (phase) {
        case "fog": return "fog"
        case "probing": case "offer": return t && t.relayState === "delegated" ? "seeds" : "dock"
        case "password": case "unlocking": return "seal"
        case "approval": return "door"
        case "connecting": case "receiving": return t && t.senderAway ? "wait" : "sail"
        case "reconnecting": case "stopped": return "wait"
        case "done": return "home"
        case "failed": return t && t.errorCode === "sender_gone" ? "empty" : ""
        default: return ""
        }
    }

    // ---------- мелкие детали языка Ferry ----------

    component Label: Text {
        color: FerryStyle.ink100
        font.family: FerryStyle.sans
        font.pixelSize: 15
        lineHeight: 22
        lineHeightMode: Text.FixedHeight
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
    component Caption: Label {
        color: FerryStyle.ink300
        font.pixelSize: 13
        lineHeight: 18
    }
    component Mono: Text {
        color: FerryStyle.ink100
        font.family: FerryStyle.mono
        font.pixelSize: 14
        textFormat: Text.PlainText
    }

    // Строка накладной: «имя ··········· значение».
    component Field: Item {
        id: field
        property string label: ""
        property string value: ""
        property string unit: ""
        property bool small: false
        width: parent ? parent.width : 0
        height: 33
        Text {
            id: fl
            anchors.verticalCenter: parent.verticalCenter
            text: field.label
            color: FerryStyle.ink200
            font.family: FerryStyle.sans
            font.pixelSize: 13
        }
        Mono {
            id: fu
            visible: field.unit !== ""
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: field.unit
            color: FerryStyle.ink300
            font.pixelSize: field.small ? 12 : 14
        }
        Mono {
            id: fv
            anchors.right: fu.visible ? fu.left : parent.right
            anchors.rightMargin: fu.visible ? 5 : 0
            anchors.verticalCenter: parent.verticalCenter
            // implicitWidth у Text не зависит от width — петли нет.
            width: Math.max(0, Math.min(implicitWidth, field.width - fl.implicitWidth - 24
                                        - (fu.visible ? fu.implicitWidth + 5 : 0)))
            text: field.value
            elide: Text.ElideMiddle
            font.pixelSize: field.small ? 12 : 14
        }
        Item {   // пунктир от подписи до значения
            id: dots
            anchors.left: fl.right
            anchors.leftMargin: 8
            anchors.right: fv.left
            anchors.rightMargin: 8
            y: Math.round(field.height / 2 + 5)
            height: 1
            clip: true
            Row {
                spacing: 2
                Repeater {
                    model: Math.max(0, Math.floor(dots.width / 3))
                    Rectangle { width: 1; height: 1; color: FerryStyle.line200; opacity: 0.6 }
                }
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: FerryStyle.line100
        }
    }

    component Button: Rectangle {
        id: btn
        property string text: ""
        property bool primary: false
        property bool small: false
        property bool enabledState: true
        signal clicked()
        implicitWidth: bl.implicitWidth + (small ? 24 : 32)
        implicitHeight: small ? 32 : 42
        radius: 4
        opacity: enabledState ? 1 : 0.55
        color: primary ? (ma.containsMouse && enabledState ? FerryStyle.signalHover : FerryStyle.signal) : "transparent"
        border.width: primary ? 0 : 1
        border.color: ma.containsMouse && enabledState ? FerryStyle.ink200 : FerryStyle.line200
        Text {
            id: bl
            anchors.centerIn: parent
            text: btn.text
            color: btn.primary ? FerryStyle.inkOnSignal : FerryStyle.ink100
            font.family: FerryStyle.sans
            font.pixelSize: btn.small ? 13 : 15
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: btn.enabledState ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (btn.enabledState) btn.clicked()
        }
    }

    // Поле ввода — тёмное, как всё в карточке.
    component Input: Rectangle {
        id: inp
        property alias text: ti.text
        property alias echoMode: ti.echoMode
        property string placeholder: ""
        signal accepted()
        width: parent ? parent.width : 0
        height: 40
        radius: 4
        color: FerryStyle.surface200
        border.width: ti.activeFocus ? 2 : 1
        border.color: ti.activeFocus ? FerryStyle.focus : FerryStyle.line200
        TextInput {
            id: ti
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            verticalAlignment: TextInput.AlignVCenter
            color: FerryStyle.ink100
            selectionColor: FerryStyle.signal
            selectedTextColor: FerryStyle.inkOnSignal
            font.family: FerryStyle.mono
            font.pixelSize: 14
            clip: true
            selectByMouse: true
            onAccepted: inp.accepted()
        }
        Text {
            visible: ti.text === "" && !ti.activeFocus
            anchors.verticalCenter: parent.verticalCenter
            x: 12
            text: inp.placeholder
            color: FerryStyle.ink300
            font.family: FerryStyle.mono
            font.pixelSize: 14
        }
    }

    // Точка-признак и подпись: «качает», «раздаёт другим».
    component Status: Item {
        id: status
        property color dot: FerryStyle.wait
        property alias text: st.text
        property color textColor: FerryStyle.ink200
        width: parent ? parent.width : 0
        height: Math.max(18, st.implicitHeight)
        Rectangle {
            y: 5
            width: 8; height: 8; radius: 2
            color: status.dot
        }
        Text {
            id: st
            x: 16
            width: status.width - 16
            wrapMode: Text.Wrap
            lineHeight: 18
            lineHeightMode: Text.FixedHeight
            color: status.textColor
            font.family: FerryStyle.sans
            font.pixelSize: 13
        }
    }

    // ---------- свёрнутая (отклонили или скрыли) ----------

    Rectangle {
        id: collapsed
        visible: root.t !== null && root.t.declined
        width: parent.width
        height: 38
        radius: 8
        color: FerryStyle.surface100
        border.width: 1
        border.color: FerryStyle.line100
        Row {
            anchors.verticalCenter: parent.verticalCenter
            x: 14
            spacing: 8
            Text {
                text: "Ferry"
                color: FerryStyle.ink200
                font.family: FerryStyle.sans
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                width: Math.min(implicitWidth, collapsed.width - 160)
                text: root.t && root.t.name !== "" ? "«" + root.t.name + "» скрыт" : "том скрыт"
                elide: Text.ElideRight
                color: FerryStyle.ink300
                font.family: FerryStyle.sans
                font.pixelSize: 13
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 14
            text: "Показать"
            color: FerryStyle.signalText
            font.family: FerryStyle.sans
            font.pixelSize: 13
            font.weight: Font.DemiBold
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: root.t.decline(false)
            }
        }
    }

    // ---------- карточка ----------

    Rectangle {
        id: card
        visible: root.t !== null && !root.t.declined
        width: parent.width
        height: col.implicitHeight + 2
        radius: 8
        color: FerryStyle.surface100
        border.width: 1
        border.color: FerryStyle.line200
        clip: true

        Column {
            id: col
            x: 1; y: 1
            width: parent.width - 2

            // Шапка: «Ferry», что происходит, справа — состояние или «закрыть».
            Item {
                width: parent.width
                height: 46
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 14
                    spacing: 10
                    Text {
                        anchors.baseline: hl.baseline
                        text: "Ferry"
                        color: FerryStyle.ink100
                        font.family: FerryStyle.sans
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        id: hl
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.headLabel.toUpperCase()
                        color: FerryStyle.ink300
                        font.family: FerryStyle.mono
                        font.pixelSize: 12
                        font.letterSpacing: 1.4
                    }
                }
                Rectangle {   // чип состояния
                    id: chipBox
                    visible: root.chip !== ""
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    width: chipRow.implicitWidth + 16
                    height: 22
                    color: "transparent"
                    border.width: 1
                    border.color: root.phase === "approval" || root.phase === "reconnecting" || root.phase === "stopped"
                                  || (root.t && root.t.senderAway) ? FerryStyle.wait : FerryStyle.signal
                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 6
                        Rectangle {
                            visible: root.phase === "approval"
                            anchors.verticalCenter: parent.verticalCenter
                            width: 6; height: 6; radius: 3
                            color: FerryStyle.wait
                        }
                        Text {
                            text: root.chip.toUpperCase()
                            color: chipBox.border.color === FerryStyle.wait ? FerryStyle.ink200 : FerryStyle.signalText
                            font.family: FerryStyle.mono
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.2
                        }
                    }
                }
                Rectangle {   // «закрыть» — свернуть карточку
                    visible: root.chip === "" && ["offer", "password", "nokey", "done", "failed"].indexOf(root.phase) >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    width: 32; height: 32; radius: 4
                    color: "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: closeMa.containsMouse ? FerryStyle.ink100 : FerryStyle.ink200
                        font.pixelSize: 14
                    }
                    MouseArea {
                        id: closeMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.t.decline(true)
                    }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 2
                    color: FerryStyle.ink100
                }
            }

            FerryScene {
                visible: root.sceneMode !== ""
                width: parent.width
                height: visible ? implicitHeight : 0
                mode: root.sceneMode !== "" ? root.sceneMode : "dock"
                progress: root.progress
                animate: root.visible && ["connecting", "receiving", "approval", "reconnecting",
                                          "probing", "unlocking"].indexOf(root.phase) >= 0
            }

            Loader {
                width: parent.width
                sourceComponent: {
                    switch (root.phase) {
                    case "fog": return fogBody
                    case "password": case "unlocking": return passwordBody
                    case "nokey": return nokeyBody
                    case "approval": return approvalBody
                    case "connecting": case "receiving": case "reconnecting": case "stopped": return receiveBody
                    case "done": return doneBody
                    case "failed": return failedBody
                    default: return offerBody
                    }
                }
            }
        }
    }

    // ---------- тела по состояниям ----------

    Component {
        id: fogBody
        Column {
            id: fogCol
            padding: 16
            spacing: 12
            width: parent ? parent.width : 0
            property bool remember: false
            Label {
                width: parent.width - 32
                text: "Похоже на раздачу Ferry"
                font.weight: Font.DemiBold
            }
            Column {
                width: parent.width - 32
                Field { label: "релей"; value: root.t ? root.t.host : "" }
            }
            Caption {
                width: parent.width - 32
                text: "Чтобы показать, что за том, нужно спросить этот релей — он увидит ваш адрес, "
                      + "как любой сайт по ссылке."
            }
            Item {   // «доверять дальше»
                width: rememberRow.implicitWidth
                height: rememberRow.implicitHeight
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: fogCol.remember = !fogCol.remember
                }
            Row {
                id: rememberRow
                spacing: 10
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16; height: 16; radius: 2
                    color: fogCol.remember ? FerryStyle.signal : "transparent"
                    border.width: fogCol.remember ? 0 : 1
                    border.color: FerryStyle.line200
                    Text {
                        visible: fogCol.remember
                        anchors.centerIn: parent
                        text: "✓"
                        color: FerryStyle.inkOnSignal
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Доверять этому релею дальше"
                    color: FerryStyle.ink200
                    font.family: FerryStyle.sans
                    font.pixelSize: 13
                }
            }
            }
            Row {
                spacing: 10
                Button {
                    primary: true
                    small: true
                    text: "Показать сведения"
                    onClicked: root.t.probe(fogCol.remember)
                }
                Button {
                    small: true
                    text: "В браузере"
                    onClicked: root.t.openInBrowser()
                }
            }
        }
    }

    Component {
        id: offerBody
        Column {
            id: offerCol
            padding: 16
            spacing: 12
            width: parent ? parent.width : 0
            readonly property bool delegated: root.t && root.t.relayState === "delegated"
            Label {
                width: parent.width - 32
                textFormat: Text.StyledText
                color: FerryStyle.ink200
                text: root.self ? "Ваша ссылка на том"
                      : "<b><font color=\"" + FerryStyle.ink100 + "\">" + root.esc(root.who) + "</font></b> "
                        + (offerCol.delegated ? "уже не на связи" : "передаёт вам том")
            }
            Column {
                visible: root.phase !== "probing"
                width: parent.width - 32
                Field { label: "имя"; value: root.t ? root.t.name : "" }
                Field {
                    label: "состав"
                    // Пустые папки здесь не считаем: место им в описи, а не в накладной.
                    value: !root.t ? "" : root.t.tree ? FerryStyle.num(root.t.files) : "1"
                    unit: !root.t ? "" : root.t.tree
                          ? FerryStyle.plural(root.t.files, "файл", "файла", "файлов") : "файл"
                }
                Field {
                    label: "размер"
                    value: root.t ? FerryStyle.size(root.t.total)[0] : ""
                    unit: root.t ? FerryStyle.size(root.t.total)[1] : ""
                }
            }
            Status {
                width: parent.width - 32
                visible: offerCol.delegated && root.phase !== "probing"
                dot: FerryStyle.seed
                textColor: FerryStyle.seedText
                text: "том держат те, кто скачал его целиком"
            }
            Caption {
                width: parent.width - 32
                text: root.phase === "probing" ? "Спрашиваем релей…"
                      : root.t && root.t.partial ? "Часть тома уже на диске — продолжим с того же места."
                      : "Шифруется на устройстве отправителя. Сохраним в «"
                        + Ferry.displayPath(Ferry.downloadDir) + "»."
                        + (root.t && root.t.requireApproval ? " Отправитель спросит, пустить ли вас." : "")
                        + (root.t && root.t.usesLeft >= 0 ? " Осталось использований: " + root.t.usesLeft + "." : "")
            }
            Row {
                visible: root.phase !== "probing"
                spacing: 10
                Button {
                    primary: true
                    width: Math.max(implicitWidth, offerCol.width - 32 - decl.width - 10)
                    text: root.t && root.t.partial ? "Продолжить" : "Принять"
                    onClicked: root.t.accept()
                }
                Button {
                    id: decl
                    text: "Отклонить"
                    onClicked: root.t.decline(true)
                }
            }
        }
    }

    Component {
        id: passwordBody
        Column {
            padding: 16
            spacing: 12
            width: parent ? parent.width : 0
            Label {
                width: parent.width - 32
                textFormat: Text.StyledText
                color: FerryStyle.ink200
                text: (root.self ? "Ваш том" : "<b><font color=\"" + FerryStyle.ink100 + "\">"
                                                + root.esc(root.who) + "</font></b> передаёт том") + " под паролем"
            }
            Column {
                width: parent.width - 32
                spacing: 6
                Text {
                    text: "ПАРОЛЬ"
                    color: FerryStyle.ink300
                    font.family: FerryStyle.sans
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                }
                Input {
                    id: pw
                    echoMode: TextInput.Password
                    placeholder: "фраза от отправителя"
                    onAccepted: if (pw.text !== "") root.t.unlock(pw.text)
                }
                Caption {
                    width: parent.width
                    text: "Фразу отправитель скажет отдельно. Регистр, лишние пробелы и «ё» не важны."
                }
                Caption {
                    visible: root.t && root.t.passwordError !== ""
                    width: parent.width
                    color: FerryStyle.dangerText
                    text: root.t ? root.t.passwordError : ""
                }
            }
            Button {
                primary: true
                enabledState: root.phase !== "unlocking"
                text: root.phase === "unlocking" ? "Считаем ключ…" : "Открыть"
                onClicked: if (pw.text !== "") root.t.unlock(pw.text)
            }
        }
    }

    Component {
        id: nokeyBody
        Column {
            id: nokeyCol
            padding: 16
            spacing: 12
            width: parent ? parent.width : 0
            property bool bad: false
            Label {
                width: parent.width - 32
                text: "Ссылка неполная"
                font.pixelSize: 19
                font.weight: Font.DemiBold
                lineHeight: 26
            }
            Caption {
                width: parent.width - 32
                color: FerryStyle.ink200
                text: "В ней нет части после #, а это и есть ключ. Использование при этом не списано."
            }
            Column {
                width: parent.width - 32
                spacing: 6
                Text {
                    text: "КЛЮЧ ИЛИ ССЫЛКА ЦЕЛИКОМ"
                    color: FerryStyle.ink300
                    font.family: FerryStyle.sans
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                }
                Input {
                    id: keyIn
                    placeholder: "#xK4m9Tz1…"
                    onAccepted: nokeyCol.bad = !root.t.useKey(keyIn.text)
                }
                Caption {
                    visible: nokeyCol.bad
                    color: FerryStyle.dangerText
                    text: "Это не похоже на ключ Ferry — нужны 43 знака после #."
                }
            }
            Item {
                width: parent.width - 32
                height: openBtn.height
                Button {
                    id: openBtn
                    primary: true
                    text: "Открыть том"
                    onClicked: nokeyCol.bad = !root.t.useKey(keyIn.text)
                }
                Mono {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "need_key"
                    color: FerryStyle.ink300
                    font.pixelSize: 12
                }
            }
        }
    }

    Component {
        id: approvalBody
        Column {
            padding: 16
            spacing: 12
            width: parent ? parent.width : 0
            Label {
                width: parent.width - 32
                textFormat: Text.StyledText
                color: FerryStyle.ink200
                text: "<b><font color=\"" + FerryStyle.ink100 + "\">" + root.esc(root.who)
                      + "</font></b> решает, пустить ли вас"
            }
            Column {
                width: parent.width - 32
                Field { label: "имя"; value: root.t ? root.t.name : "" }
                Field {
                    label: "размер"
                    value: root.t ? FerryStyle.size(root.t.total)[0] : ""
                    unit: root.t ? FerryStyle.size(root.t.total)[1] : ""
                }
            }
            Caption {
                width: parent.width - 32
                text: "Использование ссылки спишется, только когда пустят. Ушли, не дождавшись, — ничего не потрачено."
            }
            Button {
                text: "Не ждать"
                onClicked: root.t.stop()
            }
        }
    }

    Component {
        id: receiveBody
        Column {
            id: recvCol
            padding: 16
            spacing: 12
            width: parent ? parent.width : 0
            readonly property real rate: root.t ? root.t.rate : 0
            Item {
                width: parent.width - 32
                height: 36
                Row {
                    anchors.bottom: parent.bottom
                    spacing: 8
                    Mono {
                        id: pct
                        text: FerryStyle.num(root.progress * 100, 1)
                        font.pixelSize: 32
                        font.weight: Font.Medium
                    }
                    Text {
                        anchors.baseline: pct.baseline
                        text: "%"
                        color: FerryStyle.ink300
                        font.family: FerryStyle.sans
                        font.pixelSize: 15
                    }
                }
                Row {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 5
                    spacing: 5
                    Mono {
                        text: root.phase === "receiving" && recvCol.rate > 0
                              ? FerryStyle.num(recvCol.rate / 1e6, 1) : "—"
                        font.pixelSize: 15
                        font.weight: Font.Medium
                    }
                    Mono {
                        text: "МБ/с"
                        color: FerryStyle.ink300
                        font.pixelSize: 15
                    }
                }
            }
            // Карта тома: клетка — доля чанков, яркость — принято ли.
            Rectangle {
                width: parent.width - 32
                height: 20
                color: FerryStyle.surface300
                border.width: 1
                border.color: FerryStyle.line200
                Row {
                    x: 3; y: 3
                    width: parent.width - 6
                    height: 14
                    spacing: 1
                    Repeater {
                        id: cellRep
                        model: root.t ? root.t.cells : []
                        delegate: Rectangle {
                            required property var modelData
                            width: (parent.width - (cellRep.count - 1)) / Math.max(1, cellRep.count)
                            height: 14
                            color: modelData === 3 ? FerryStyle.chunkPeer
                                   : modelData === 2 ? FerryStyle.chunkHave
                                   : modelData === 1 ? FerryStyle.chunkInflight : FerryStyle.chunkNone
                        }
                    }
                }
            }
            Column {
                width: parent.width - 32
                Field {
                    label: "принято"
                    value: root.t ? FerryStyle.size(root.t.bytes).join(" ") : ""
                    unit: root.t ? "из " + FerryStyle.size(root.t.total).join(" ") : ""
                }
                Field {
                    label: "осталось"
                    value: !root.t || root.phase !== "receiving" || recvCol.rate <= 1 ? "—"
                           : FerryStyle.eta((root.t.total - root.t.bytes) / recvCol.rate * 1000)
                }
            }
            Item {
                width: parent.width - 32
                height: Math.max(stopBtn.height, recvStatus.height)
                Status {
                    id: recvStatus
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - stopBtn.width - 12
                    dot: root.phase === "receiving" && !(root.t && root.t.senderAway) ? FerryStyle.signal : FerryStyle.wait
                    text: {
                        if (!root.t) return ""
                        switch (root.phase) {
                        case "connecting": return "связываемся с релеем"
                        case "reconnecting": return "связь пропала · снова через " + Math.ceil(root.t.reconnectInMs / 1000) + " с"
                        case "stopped": return "остановлено · принятое на диске"
                        default:
                            if (root.t.senderAway)
                                return root.who + " переподключается · ждём ещё " + Math.ceil(root.t.senderWaitMs / 1000) + " с"
                            if (root.t.relayState === "delegated")
                                return "отдают скачавшие"
                            return "качает"
                        }
                    }
                }
                Button {
                    id: stopBtn
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    primary: root.phase === "stopped"
                    text: root.phase === "stopped" ? "Продолжить" : "Остановить"
                    onClicked: root.phase === "stopped" ? root.t.accept() : root.t.stop()
                }
            }
        }
    }

    Component {
        id: doneBody
        Column {
            id: doneCol
            padding: 16
            spacing: 12
            width: parent ? parent.width : 0
            Row {
                spacing: 14
                Item {   // штамп «доставлено» — двойная рамка, чуть наискось
                    id: stampBox
                    width: stamp.implicitWidth + 30
                    height: 34
                    rotation: -4
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.width: 1
                        border.color: FerryStyle.ok
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 2
                            color: "transparent"
                            border.width: 1
                            border.color: FerryStyle.ok
                        }
                    }
                    Text {
                        id: stamp
                        anchors.centerIn: parent
                        text: "ДОСТАВЛЕНО"
                        color: FerryStyle.okText
                        font.family: FerryStyle.mono
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        font.letterSpacing: 2
                    }
                }
                Caption {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, doneCol.width - 32 - stampBox.width - 14)
                    color: FerryStyle.ink200
                    text: root.t && root.t.count > 0
                          ? FerryStyle.num(root.t.count) + " из " + FerryStyle.num(root.t.count) + " чанков проверено BLAKE3"
                          : "каждый чанк проверен BLAKE3"
                }
            }
            Column {
                width: parent.width - 32
                Field {
                    label: root.t && root.t.tree ? "папка" : "файл"
                    value: root.t ? Ferry.displayPath(root.t.finalPath) : ""
                    small: true
                }
            }
            Status {
                width: parent.width - 32
                visible: root.t && root.t.seeding
                dot: FerryStyle.seed
                textColor: FerryStyle.seedText
                text: Ferry.seedMode === "app" ? "раздаёт другим, пока открыто приложение"
                                               : "раздаёт другим, пока открыт звонок"
            }
            Text {
                visible: root.t && !root.t.seeding && Ferry.seedMode !== "off"
                text: "Раздавать другим"
                color: FerryStyle.signalText
                font.family: FerryStyle.sans
                font.pixelSize: 13
                font.weight: Font.DemiBold
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.t.setSeeding(true)
                }
            }
            Row {
                spacing: 10
                Button {
                    primary: true
                    width: Math.max(implicitWidth, doneCol.width - 32 - hideBtn.width - 10)
                    text: "Открыть папку"
                    onClicked: root.t.openFolder()
                }
                Button {
                    id: hideBtn
                    text: "Скрыть"
                    onClicked: root.t.decline(true)
                }
            }
        }
    }

    Component {
        id: failedBody
        Column {
            id: failCol
            padding: 16
            spacing: 10
            width: parent ? parent.width : 0
            readonly property bool askable: !root.self && root.t
                && ["sender_gone", "uses_exhausted", "expired", "revoked", "transfer_not_found",
                    "approval_denied", "no_source"].indexOf(root.t.errorCode) >= 0
            Item {   // штамп отказа
                visible: root.t && root.t.errorCode === "sender_gone"
                width: gone.implicitWidth + 24
                height: 30
                rotation: -3
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 1
                    border.color: FerryStyle.danger
                }
                Text {
                    id: gone
                    anchors.centerIn: parent
                    text: "РЕЙС ОКОНЧЕН"
                    color: FerryStyle.dangerText
                    font.family: FerryStyle.mono
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    font.letterSpacing: 2
                }
            }
            Label {
                width: parent.width - 32
                text: root.t ? root.t.errorTitle : ""
                font.weight: Font.DemiBold
            }
            Caption {
                visible: text !== ""
                width: parent.width - 32
                color: FerryStyle.ink200
                text: root.t ? root.t.errorText : ""
            }
            Caption {   // что делать дальше
                visible: root.t && root.t.errorWay !== ""
                width: parent.width - 32
                textFormat: Text.StyledText
                color: FerryStyle.ink100
                text: "<font color=\"" + FerryStyle.ink300 + "\">выход</font>&nbsp;&nbsp;"
                      + root.esc(root.t ? root.t.errorWay : "")
            }
            Item {
                width: parent.width - 32
                height: Math.max(failBtns.height, 20)
                Row {
                    id: failBtns
                    spacing: 10
                    Button {
                        visible: root.t && root.t.retryable
                        primary: true
                        small: true
                        text: "Попробовать снова"
                        onClicked: root.t.retry()
                    }
                    Button {
                        visible: failCol.askable
                        small: true
                        text: "Попросить в чате"
                        onClicked: root.ask((root.t.name !== "" ? "Том «" + root.t.name + "»" : "Том по ссылке Ferry")
                                            + " больше не забрать — переотправите?")
                    }
                }
                Mono {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.t ? root.t.errorCode : ""
                    color: FerryStyle.ink300
                    font.pixelSize: 12
                }
            }
        }
    }
}
