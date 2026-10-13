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

    // Своя раздача — карточка отправителя (пульт); чужая — получателя.
    readonly property var send: url !== "" && root.self ? Ferry.outgoing(url) : null
    readonly property var t: url !== "" && !send ? Ferry.transfer(url) : null
    readonly property string phase: t ? t.phase : ""

    visible: t !== null || send !== null
    implicitHeight: send ? sendCard.height : !t ? 0 : t.declined ? collapsed.height : card.height

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
        property bool danger: false
        property bool small: false
        property bool enabledState: true
        signal clicked()
        implicitWidth: bl.implicitWidth + (small ? 24 : 32)
        implicitHeight: small ? 32 : 42
        radius: 4
        opacity: enabledState ? 1 : 0.55
        color: danger ? (ma.containsMouse && enabledState ? "#d8443f" : FerryStyle.danger)
               : primary ? (ma.containsMouse && enabledState ? FerryStyle.signalHover : FerryStyle.signal)
               : "transparent"
        border.width: primary || danger ? 0 : 1
        border.color: ma.containsMouse && enabledState ? FerryStyle.ink200 : FerryStyle.line200
        Text {
            id: bl
            anchors.centerIn: parent
            text: btn.text
            color: btn.danger ? "#fff2f0" : btn.primary ? FerryStyle.inkOnSignal : FerryStyle.ink100
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
        visible: root.t !== null && root.t.declined && !root.send
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
        visible: root.t !== null && !root.t.declined && !root.send
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
                Row {   // справа: скопировать ссылку, состояние или «закрыть»
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: root.chip !== "" ? 14 : 8
                    spacing: 4
                    // Ссылки в пузыре сообщения нет (её место заняла карточка),
                    // а переслать её бывает нужно — отсюда.
                    Rectangle {
                        id: copyBtn
                        property bool copied: false
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32; height: 32; radius: 4
                        color: "transparent"
                        AppIcon {
                            anchors.centerIn: parent
                            name: copyBtn.copied ? "check" : "copy"
                            size: 15
                            color: copyBtn.copied ? FerryStyle.okText
                                   : copyMa.containsMouse ? FerryStyle.ink100 : FerryStyle.ink200
                        }
                        MouseArea {
                            id: copyMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Sys.copyText(root.url)
                                copyBtn.copied = true
                                copiedTimer.restart()
                            }
                        }
                        Timer {
                            id: copiedTimer
                            interval: 1500
                            onTriggered: copyBtn.copied = false
                        }
                    }
                    Rectangle {   // чип состояния
                        id: chipBox
                        visible: root.chip !== ""
                        anchors.verticalCenter: parent.verticalCenter
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


    // ---------- своя раздача: пульт отправителя ----------

    property bool sendPanel: false     // раскрыты «условия на ходу»
    property bool ownerCopied: false
    readonly property bool sendLive: send !== null && ["live", "lost", "creating", "hashing", "offering"].indexOf(send.phase) >= 0
    readonly property bool sendDone: send !== null && (send.phase === "closed" || send.phase === "revoked")
    readonly property real sendProgress: {
        if (!send || send.receivers.length === 0) return 0
        let best = 0
        for (let i = 0; i < send.receivers.length; i++)
            best = Math.max(best, send.receivers[i].progress)
        return best
    }
    // Тикает раз в минуту ради «ещё 23 ч»: срок — от часов, а не от сигналов.
    property double nowMs: Date.now()
    Timer {
        running: root.send !== null && root.sendLive
        interval: 30000
        repeat: true
        onTriggered: root.nowMs = Date.now()
    }
    function hoursLeft() {
        if (!send || send.expiresAt <= 0) return -1
        return Math.max(0, (send.expiresAt - root.nowMs) / 3600000)
    }
    function ttlText() {
        const h = hoursLeft()
        if (h < 0) return "срок по релею"
        if (h >= 48) return "ещё " + Math.round(h / 24) + " " + FerryStyle.plural(Math.round(h / 24), "день", "дня", "дней")
        if (h >= 1) return "ещё " + Math.floor(h) + " ч"
        return "ещё " + Math.max(1, Math.round(h * 60)) + " мин"
    }
    function usesText() {
        if (!send || send.usesLeft < 0) return "без лимита"
        return send.usesLeft + " " + FerryStyle.plural(send.usesLeft, "раз", "раза", "раз")
    }
    function roleText(r) { return r === "seed" ? "раздаёт" : r === "catching" ? "догоняет" : "качает" }

    Rectangle {
        id: sendCard
        visible: root.send !== null
        width: parent.width
        height: sendCol.implicitHeight + 2
        radius: 8
        color: FerryStyle.surface100
        border.width: 1
        border.color: FerryStyle.line200
        clip: true

        Column {
            id: sendCol
            x: 1; y: 1
            width: parent.width - 2

            Item {   // шапка
                width: parent.width
                height: 46
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 14
                    spacing: 10
                    Text {
                        text: "Ferry"
                        color: FerryStyle.ink100
                        font.family: FerryStyle.sans
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: !root.send ? "" : root.sendPanel ? "УСЛОВИЯ ВЫДАЧИ"
                              : root.sendDone ? "КВИТАНЦИЯ"
                              : root.send.phase === "failed" ? "ОТКАЗ В ПЕРЕВОЗКЕ" : "ВАША РАЗДАЧА"
                        color: FerryStyle.ink300
                        font.family: FerryStyle.mono
                        font.pixelSize: 12
                        font.letterSpacing: 1.4
                    }
                }
                Rectangle {
                    id: sendChip
                    readonly property bool waiting: root.send && (root.send.knocks.length > 0 || root.send.phase === "lost")
                    visible: root.send && root.send.phase !== "failed" && !root.sendPanel
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    width: sendChipText.implicitWidth + 16
                    height: 22
                    color: "transparent"
                    border.width: 1
                    border.color: root.sendDone ? FerryStyle.ok : waiting ? FerryStyle.wait : FerryStyle.signal
                    Text {
                        id: sendChipText
                        anchors.centerIn: parent
                        text: !root.send ? "" : root.sendDone ? (root.send.delivered.length > 0 ? "ДОСТАВЛЕНО" : "ЗАКРЫТО")
                              : root.send.knocks.length > 0 ? root.send.knocks.length + " У ДВЕРИ"
                              : root.send.phase === "lost" ? "СВЯЗЬ" : "ИДЁТ"
                        color: root.sendDone ? FerryStyle.okText : sendChip.waiting ? FerryStyle.ink200 : FerryStyle.signalText
                        font.family: FerryStyle.mono
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.2
                    }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width; height: 2
                    color: FerryStyle.ink100
                }
            }

            FerryScene {
                visible: root.send !== null && root.send.phase !== "failed" && !root.sendPanel
                width: parent.width
                height: visible ? 64 : 0
                mode: root.sendDone ? "home" : root.send && root.send.phase === "lost" ? "wait" : "sail"
                progress: root.sendProgress
                animate: root.visible && root.sendLive
            }

            Loader {
                width: parent.width
                sourceComponent: !root.send ? null
                                 : root.send.phase === "failed" ? sendFailedBody
                                 : root.sendDone ? receiptBody
                                 : root.sendPanel ? policyBody : liveBody
            }
        }
    }

    Component {
        id: liveBody
        Column {
            id: liveCol
            padding: 14
            spacing: 12
            width: parent ? parent.width : 0
            Column {
                spacing: 2
                Mono { text: root.send.name; width: liveCol.width - 28; elide: Text.ElideMiddle }
                Caption {
                    text: (root.send.tree ? FerryStyle.num(root.send.files) + " "
                           + FerryStyle.plural(root.send.files, "файл", "файла", "файлов") + " · " : "")
                          + FerryStyle.size(root.send.total).join(" ")
                }
            }
            Column {   // хеши, пока считаются
                visible: !root.send.hashesDone && root.send.count > 0
                width: liveCol.width - 28
                spacing: 6
                Item {
                    width: parent.width
                    height: 16
                    Caption { text: "хеши"; color: FerryStyle.ink200 }
                    Mono {
                        anchors.right: parent.right
                        text: FerryStyle.num(root.send.hashed) + " из " + FerryStyle.num(root.send.count)
                        font.pixelSize: 12
                    }
                }
                Rectangle {
                    width: parent.width; height: 4
                    color: FerryStyle.surface300
                    Rectangle {
                        width: parent.width * (root.send.count > 0 ? root.send.hashed / root.send.count : 0)
                        height: 4
                        color: FerryStyle.line200
                    }
                }
            }
            // У двери: первый в очереди — с кнопками, остальные — списком.
            Column {
                visible: root.send.knocks.length > 0
                width: liveCol.width - 28
                spacing: 8
                Label {
                    width: parent.width
                    textFormat: Text.StyledText
                    color: FerryStyle.ink200
                    text: root.send.knocks.length > 0
                          ? "<b><font color=\"" + FerryStyle.ink100 + "\">" + root.esc(root.send.knocks[0].name || "Без имени")
                            + "</font></b> просит том" : ""
                }
                Caption { width: parent.width; text: "Ключ верный. Пустите — спишется одно использование." }
                Row {
                    spacing: 10
                    Button {
                        primary: true
                        small: true
                        text: "Пустить"
                        onClicked: root.send.approve(root.send.knocks[0].id, true)
                    }
                    Button {
                        small: true
                        text: "Отказать"
                        onClicked: root.send.approve(root.send.knocks[0].id, false)
                    }
                }
                Item {
                    visible: root.send.knocks.length > 1
                    width: parent.width
                    height: 18
                    Caption {
                        width: parent.width - allBtn.width - 10
                        elide: Text.ElideRight
                        text: "ещё ждёт: " + root.send.knocks.slice(1).map(function (k) { return k.name || "без имени" }).join(", ")
                    }
                    FerryKit.TextAction {
                        id: allBtn
                        anchors.right: parent.right
                        text: "Пускать всех"
                        onClicked: root.send.approveAll()
                    }
                }
            }
            Column {   // получатели
                width: liveCol.width - 28
                Item {
                    width: parent.width
                    height: 22
                    FerryKit.SmallLabel { text: "ПОЛУЧАТЕЛИ" }
                    FerryKit.SmallLabel {
                        anchors.right: parent.right
                        text: root.send.receivers.length + " из " + (root.send.maxConcurrent > 0 ? root.send.maxConcurrent : 8) + " мест"
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: FerryStyle.line200 }
                }
                Caption {
                    visible: root.send.receivers.length === 0
                    topPadding: 8
                    width: parent.width
                    text: "Пока никто не забрал. Ссылка в чате — ждём."
                }
                Repeater {
                    model: root.send.receivers
                    delegate: Item {
                        required property var modelData
                        width: parent.width
                        height: 36
                        Text {
                            id: rName
                            anchors.verticalCenter: parent.verticalCenter
                            width: 56
                            elide: Text.ElideRight
                            text: modelData.name
                            color: FerryStyle.ink100
                            font.family: FerryStyle.sans
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            x: 64
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 64 - 8 - 40 - 8 - 76
                            height: 6
                            color: FerryStyle.surface300
                            Rectangle {
                                width: parent.width * Math.min(1, modelData.progress)
                                height: 6
                                color: modelData.role === "catching" ? FerryStyle.chunkPeer : FerryStyle.chunkHave
                                Behavior on width { NumberAnimation { duration: 600 } }
                            }
                        }
                        Mono {
                            anchors.verticalCenter: parent.verticalCenter
                            x: parent.width - 76 - 8 - 40
                            width: 40
                            horizontalAlignment: Text.AlignRight
                            text: Math.round(modelData.progress * 100) + "%"
                            font.pixelSize: 12
                        }
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            spacing: 6
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 7; height: 7; radius: 2
                                color: modelData.role === "seed" ? FerryStyle.seed
                                       : modelData.role === "catching" ? FerryStyle.chunkInflight : FerryStyle.wait
                            }
                            Mono {
                                text: root.roleText(modelData.role)
                                color: modelData.role === "seed" ? FerryStyle.seedText : FerryStyle.ink200
                                font.pixelSize: 12
                            }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: FerryStyle.line100 }
                    }
                }
            }
            Column {   // откуда байты
                visible: root.send.srcWindow + root.send.srcPeers + root.send.srcSender > 0
                width: liveCol.width - 28
                spacing: 6
                Rectangle {
                    width: parent.width; height: 10
                    color: FerryStyle.surface300
                    Row {
                        x: 2; y: 2
                        spacing: 2
                        Rectangle { width: (parent.parent.width - 8) * root.send.srcWindow; height: 6; color: FerryStyle.chunkHave }
                        Rectangle { width: (parent.parent.width - 8) * root.send.srcPeers; height: 6; color: FerryStyle.chunkPeer }
                        Rectangle { width: (parent.parent.width - 8) * root.send.srcSender; height: 6; color: "#ff9a52" }
                    }
                }
                Text {
                    textFormat: Text.StyledText
                    color: FerryStyle.ink300
                    font.family: FerryStyle.sans
                    font.pixelSize: 13
                    text: "окно <b><font color=\"" + FerryStyle.ink100 + "\">" + Math.round(root.send.srcWindow * 100) + " %</font></b>"
                          + "&nbsp;&nbsp; другие <b><font color=\"" + FerryStyle.seedText + "\">" + Math.round(root.send.srcPeers * 100) + " %</font></b>"
                          + "&nbsp;&nbsp; вы <b><font color=\"" + FerryStyle.ink100 + "\">" + Math.round(root.send.srcSender * 100) + " %</font></b>"
                }
            }
            Mono {
                color: FerryStyle.ink200
                font.pixelSize: 12
                text: root.usesText() + " · " + root.ttlText()
                      + (root.send.maxConcurrent > 0 ? " · не более " + root.send.maxConcurrent : "")
            }
            Status {
                visible: root.send.phase === "lost"
                width: liveCol.width - 28
                text: "Связь с релеем пропала — возвращаемся; получатели подождут."
            }
            Row {
                spacing: 8
                Button {
                    width: Math.max(implicitWidth, liveCol.width - 28 - closeAccess.width - 8)
                    text: "Условия"
                    onClicked: root.sendPanel = true
                }
                Button {
                    id: closeAccess
                    danger: true
                    text: "Закрыть доступ"
                    onClicked: root.send.revoke()
                }
            }
            Caption {
                width: liveCol.width - 28
                text: "Раздача идёт, пока открыт MeetUp, — и после выхода из звонка тоже."
            }
        }
    }

    Component {
        id: policyBody
        Column {
            id: polCol
            padding: 14
            spacing: 12
            width: parent ? parent.width : 0
            Item {
                width: polCol.width - 28
                height: 18
                Caption { text: "использований"; color: FerryStyle.ink200 }
                Mono { anchors.right: parent.right; text: root.usesText(); font.pixelSize: 12 }
            }
            FerryKit.Seg {
                width: polCol.width - 28
                labels: ["+1", "+5", "без лимита"]
                current: -1
                onPicked: function (i) {
                    const now = root.send.usesLeft < 0 ? 0 : root.send.usesLeft
                    root.send.setUses(i === 0 ? now + 1 : i === 1 ? now + 5 : -1)
                }
            }
            Item {
                width: polCol.width - 28
                height: 18
                Caption { text: "срок"; color: FerryStyle.ink200 }
                Mono { anchors.right: parent.right; text: root.ttlText(); font.pixelSize: 12 }
            }
            FerryKit.Seg {
                width: polCol.width - 28
                labels: ["+1 ч", "+24 ч", "неделя"]
                current: -1
                onPicked: function (i) {
                    const left = Math.max(0, root.hoursLeft())
                    root.send.setTtlHours(i === 0 ? left + 1 : i === 1 ? left + 24 : 168)
                }
            }
            Column {
                width: polCol.width - 28
                Rectangle { width: parent.width; height: 1; color: FerryStyle.line100 }
                FerryKit.Toggle {
                    title: "Спрашивать, кого пускать"
                    hint: "Вопрос придёт вам в звонок — в карточке и уведомлением."
                    on: root.send.requireApproval
                    onFlipped: root.send.setApproval(!root.send.requireApproval)
                }
                FerryKit.Toggle {
                    title: "Раздача без меня"
                    hint: "Уйдёте или закроете MeetUp — том отдадут те, кто скачал целиком."
                    on: root.send.leaveOpen
                    onFlipped: root.send.setLeaveOpen(!root.send.leaveOpen)
                }
            }
            Column {
                width: polCol.width - 28
                spacing: 6
                FerryKit.SmallLabel { text: "ССЫЛКА ВЛАДЕЛЬЦА" }
                Item {
                    width: parent.width
                    height: 32
                    Mono {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - copyOwner.width - 10
                        elide: Text.ElideRight
                        text: root.send.ownerLink.replace(/^https?:\/\//, "")
                        font.pixelSize: 12
                    }
                    Button {
                        id: copyOwner
                        anchors.right: parent.right
                        small: true
                        text: root.ownerCopied ? "Скопировано" : "Скопировать"
                        onClicked: {
                            Sys.copyText(root.send.ownerLink)
                            root.ownerCopied = true
                            ownerCopiedTimer.restart()
                        }
                        Timer { id: ownerCopiedTimer; interval: 1500; onTriggered: root.ownerCopied = false }
                    }
                }
                Caption {
                    width: parent.width
                    text: "С ней можно управлять раздачей из браузера, когда MeetUp закрыт. В чат её не отправляйте."
                }
            }
            Button {
                primary: true
                text: "Готово"
                onClicked: root.sendPanel = false
            }
        }
    }

    Component {
        id: receiptBody
        Column {
            id: rcCol
            padding: 14
            spacing: 10
            width: parent ? parent.width : 0
            Mono { text: root.send.name; width: rcCol.width - 28; elide: Text.ElideMiddle }
            Column {
                width: rcCol.width - 28
                Field { label: "вы отдали"; value: FerryStyle.size(root.send.sentBytes)[0]; unit: FerryStyle.size(root.send.sentBytes)[1] }
                Field {
                    label: "получили"
                    value: root.send.delivered.length > 0 ? root.send.delivered.join(", ") : "никто"
                    small: true
                }
            }
            Caption {
                width: rcCol.width - 28
                text: (root.send.phase === "revoked" ? "Вы закрыли доступ в " : "Вы ушли в ")
                      + Qt.formatTime(new Date(root.send.closedAt), "HH:mm") + ". "
                      + (root.send.phase === "closed" && root.send.leaveOpen && root.send.delivered.length > 0
                         ? "Том остался у скачавших — раздают они."
                         : "На релее не осталось ничего.")
            }
        }
    }

    Component {
        id: sendFailedBody
        Column {
            id: sfCol
            padding: 14
            spacing: 10
            width: parent ? parent.width : 0
            Label { width: sfCol.width - 28; text: root.send.errorTitle; font.weight: Font.DemiBold }
            Caption { visible: text !== ""; width: sfCol.width - 28; color: FerryStyle.ink200; text: root.send.errorText }
            Mono { text: root.send.errorCode; color: FerryStyle.ink300; font.pixelSize: 12 }
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
