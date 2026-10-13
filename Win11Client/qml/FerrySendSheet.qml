import QtQuick
import QtQuick.Dialogs
import MeetUp

// Лист погрузки Ferry — поверх звонка, по скрепке или перетаскиванию (доска
// «Лист отправки»). Все условия Ferry здесь, умолчания под звонок: в комнате
// без сквозного шифрования ссылку с ключом прочтёт и сервер MeetUp, поэтому
// «спрашивать, кого пускать» там включено заранее.
Item {
    id: root

    property var draft: null              // FerryDraft (Ferry.prepare)
    property bool e2e: false
    signal finished()

    visible: draft !== null
    z: 300

    // ---- условия ----
    property int usesIdx: 2               // 1 / 5 / без лимита / своё
    property string customUses: "3"
    property int ttlIdx: 1                // 1 ч / 24 ч / 7 дней
    property int conc: 8
    property bool approve: !e2e
    property bool leave: true
    property bool pwOn: false
    property bool relayEditing: false
    property bool showSkipped: false

    onDraftChanged: {
        usesIdx = 2; customUses = "3"; ttlIdx = 1; approve = !e2e; leave = true
        pwOn = false; relayEditing = false; showSkipped = false
        pwIn.text = ""
    }
    readonly property int relayMax: draft ? draft.relayMaxConcurrent : 8
    onRelayMaxChanged: conc = relayMax

    function close() {
        const d = root.draft
        root.draft = null
        if (d) Ferry.release(d)
        root.finished()
    }

    readonly property string pwText: pwIn.text
    readonly property string pwNorm: pwText.replace(/\s+/g, " ").trim()
    readonly property int pwWords: pwNorm === "" ? 0 : pwNorm.split(" ").length
    readonly property int pwChars: pwNorm.length
    readonly property bool pwOk: pwOn && pwText !== "" && draft && draft.passwordProblem(pwText) === ""

    readonly property bool ready: draft !== null && !draft.scanning && draft.error === ""
                                  && draft.relay !== "" && draft.relayStatus === "ok" && !draft.sending
                                  && (!pwOn || pwOk)
                                  && (usesIdx !== 3 || parseInt(customUses) > 0)

    function send() {
        if (!ready) return
        draft.send({
            uses: usesIdx === 0 ? 1 : usesIdx === 1 ? 5 : usesIdx === 2 ? -1 : parseInt(customUses),
            ttlHours: ttlIdx === 0 ? 1 : ttlIdx === 1 ? 24 : 168,
            maxConcurrent: conc,
            requireApproval: approve && draft.relayPolicy,
            leaveOpen: leave && draft.relayPolicy,
            password: pwOn ? pwText : ""
        })
    }

    Connections {
        target: root.draft
        ignoreUnknownSignals: true
        function onSent() { root.close() }
    }

    // ---------- мелочи ----------

    // ---------- затемнение и лист ----------

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(4 / 255, 8 / 255, 11 / 255, 0.74)
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    Rectangle {
        id: sheet
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 32)
        height: Math.min(col.implicitHeight + 2, parent.height - 48)
        radius: 8
        color: FerryStyle.surface100
        border.width: 1
        border.color: FerryStyle.line200
        clip: true
        MouseArea { anchors.fill: parent }   // клики по листу не закрывают его

        Flickable {
            anchors.fill: parent
            anchors.margins: 1
            contentHeight: col.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: col
                width: parent.width

                // Шапка — как у карточки.
                Item {
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
                            text: "ПОГРУЗКА"
                            color: FerryStyle.ink300
                            font.family: FerryStyle.mono
                            font.pixelSize: 12
                            font.letterSpacing: 1.4
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        anchors.rightMargin: 18
                        text: "✕"
                        color: closeMa.containsMouse ? FerryStyle.ink100 : FerryStyle.ink200
                        font.pixelSize: 14
                        MouseArea {
                            id: closeMa
                            anchors.fill: parent
                            anchors.margins: -9
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.close()
                        }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 2
                        color: FerryStyle.ink100
                    }
                }

                Column {
                    width: parent.width
                    padding: 16
                    spacing: 18

                    // ---- груз ----
                    Column {
                        width: parent.width - 32
                        spacing: 8
                        Item {
                            width: parent.width
                            height: 20
                            FerryKit.SmallLabel { anchors.verticalCenter: parent.verticalCenter; text: "ГРУЗ" }
                            Row {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 5
                                visible: root.draft && !root.draft.scanning && root.draft.error === ""
                                FerryCard.Mono {
                                    text: root.draft ? FerryStyle.num(root.draft.files) : ""
                                    font.pixelSize: 15
                                }
                                FerryCard.Mono {
                                    text: root.draft ? FerryStyle.plural(root.draft.files, "файл", "файла", "файлов") + " ·" : ""
                                    color: FerryStyle.ink300
                                    font.pixelSize: 15
                                }
                                FerryCard.Mono {
                                    text: root.draft ? FerryStyle.size(root.draft.total)[0] : ""
                                    font.pixelSize: 15
                                }
                                FerryCard.Mono {
                                    text: root.draft ? FerryStyle.size(root.draft.total)[1] : ""
                                    color: FerryStyle.ink300
                                    font.pixelSize: 15
                                }
                            }
                            FerryCard.Mono {
                                visible: root.draft && root.draft.scanning
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: "собираем опись…"
                                color: FerryStyle.ink300
                                font.pixelSize: 13
                            }
                        }
                        Column {
                            width: parent.width
                            visible: root.draft && !root.draft.scanning
                            Rectangle { width: parent.width; height: 1; color: FerryStyle.line100 }
                            Repeater {
                                model: root.draft ? root.draft.items.slice(0, 5) : []
                                delegate: Item {
                                    required property var modelData
                                    width: parent.width
                                    height: 34
                                    AppIcon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: modelData.dir ? "folder" : "file"
                                        size: 14
                                        color: FerryStyle.ink300
                                    }
                                    FerryCard.Mono {
                                        x: 24
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - 24 - itemSize.implicitWidth - 12
                                        elide: Text.ElideMiddle
                                        text: modelData.name
                                    }
                                    FerryCard.Mono {
                                        id: itemSize
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.files !== undefined
                                              ? FerryStyle.num(modelData.files) + " " + FerryStyle.plural(modelData.files, "файл", "файла", "файлов")
                                              : FerryStyle.size(modelData.size).join(" ")
                                        color: FerryStyle.ink200
                                        font.pixelSize: 12
                                    }
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width; height: 1
                                        color: FerryStyle.line100
                                    }
                                }
                            }
                            FerryCard.Caption {
                                visible: root.draft && root.draft.items.length > 5
                                topPadding: 6
                                text: root.draft ? "и ещё " + (root.draft.items.length - 5) : ""
                            }
                        }
                        // Что не поедет — до ссылки, а не молча.
                        Column {
                            width: parent.width
                            spacing: 4
                            visible: root.draft && root.draft.skipped.length > 0
                            Row {
                                spacing: 8
                                FerryCard.Caption {
                                    color: FerryStyle.warnText
                                    text: root.draft ? "Не поедут " + root.draft.skipped.length + ": ярлыки, "
                                                       + "нечитаемое и имена, которые не пройдут на той стороне."
                                                     : ""
                                    width: Math.min(implicitWidth, sheet.width - 32 - 70)
                                }
                                FerryKit.TextAction {
                                    text: root.showSkipped ? "Скрыть" : "Какие"
                                    onClicked: root.showSkipped = !root.showSkipped
                                }
                            }
                            Repeater {
                                model: root.showSkipped && root.draft ? root.draft.skipped.slice(0, 20) : []
                                delegate: FerryCard.Mono {
                                    required property string modelData
                                    width: sheet.width - 32
                                    elide: Text.ElideMiddle
                                    text: modelData
                                    color: FerryStyle.ink300
                                    font.pixelSize: 12
                                }
                            }
                        }
                        FerryCard.Caption {
                            visible: root.draft && root.draft.error !== ""
                            width: parent.width
                            color: FerryStyle.dangerText
                            text: root.draft ? root.draft.error : ""
                        }
                        Row {
                            spacing: 16
                            FerryKit.TextAction { text: "+ файлы"; onClicked: addFiles.open() }
                            FerryKit.TextAction { text: "+ папка"; onClicked: addFolder.open() }
                        }
                    }

                    // ---- условия выдачи ----
                    Column {
                        width: parent.width - 32
                        spacing: 14
                        Row {
                            width: parent.width
                            spacing: 12
                            FerryKit.SmallLabel {
                                id: condLabel
                                text: "УСЛОВИЯ ВЫДАЧИ"
                                color: FerryStyle.ink100
                                font.weight: Font.DemiBold
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - condLabel.width - 12
                                height: 1
                                color: FerryStyle.line100
                            }
                        }
                        Grid {
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 12
                            verticalItemAlignment: Grid.AlignVCenter
                            FerryCard.Caption { width: 100; color: FerryStyle.ink200; text: "использований" }
                            FerryKit.Seg {
                                width: sheet.width - 32 - 2 - 100 - 12
                                labels: ["1", "5", "без лимита", "своё"]
                                weights: [0.7, 0.7, 1.7, 1]
                                current: root.usesIdx
                                onPicked: function (i) { root.usesIdx = i }
                            }
                            Item { width: 100; height: 1; visible: root.usesIdx === 3 }
                            FerryCard.Input {
                                visible: root.usesIdx === 3
                                width: 90
                                text: root.customUses
                                onTextChanged: root.customUses = text.replace(/[^0-9]/g, "")
                            }
                            FerryCard.Caption { width: 100; color: FerryStyle.ink200; text: "срок" }
                            FerryKit.Seg {
                                width: sheet.width - 32 - 2 - 100 - 12
                                labels: ["1 ч", "24 ч", "7 дней"]
                                current: root.ttlIdx
                                onPicked: function (i) { root.ttlIdx = i }
                            }
                            FerryCard.Caption { width: 100; color: FerryStyle.ink200; text: "одновременно" }
                            Row {
                                spacing: 10
                                Rectangle {
                                    width: 92; height: 32
                                    radius: 4
                                    color: FerryStyle.surface200
                                    border.width: 1
                                    border.color: FerryStyle.line200
                                    Text {
                                        x: 0; width: 32; height: parent.height
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        text: "−"
                                        color: FerryStyle.ink100
                                        font.pixelSize: 15
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.conc = Math.max(1, root.conc - 1)
                                        }
                                    }
                                    FerryCard.Mono {
                                        anchors.centerIn: parent
                                        text: root.conc
                                        font.pixelSize: 15
                                    }
                                    Text {
                                        x: 60; width: 32; height: parent.height
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        text: "+"
                                        color: FerryStyle.ink100
                                        font.pixelSize: 15
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.conc = Math.min(root.relayMax, root.conc + 1)
                                        }
                                    }
                                }
                                FerryCard.Caption {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "потолок этого релея — " + root.relayMax
                                }
                            }
                        }
                        Column {
                            width: parent.width
                            Rectangle { width: parent.width; height: 1; color: FerryStyle.line100 }
                            FerryKit.Toggle {
                                title: "Спрашивать, кого пускать"
                                hint: root.draft && !root.draft.relayPolicy
                                      ? "Этот релей так не умеет."
                                      : "Вопрос придёт вам в звонок — в карточке и уведомлением."
                                on: root.approve && root.draft && root.draft.relayPolicy
                                enabledState: root.draft && root.draft.relayPolicy
                                onFlipped: root.approve = !root.approve
                            }
                            FerryKit.Toggle {
                                title: "Раздача без меня"
                                hint: "Уйдёте или закроете MeetUp — том отдадут те, кто скачал целиком. "
                                      + "Нет таких — раздача закроется."
                                on: root.leave && root.draft && root.draft.relayPolicy
                                enabledState: root.draft && root.draft.relayPolicy
                                onFlipped: root.leave = !root.leave
                            }
                            FerryKit.Toggle {
                                title: "Пароль вместо ключа"
                                hint: root.draft && !root.draft.relayPassword
                                      ? "Этот релей так не умеет."
                                      : root.pwOn
                                        ? "Слабее ключа: у кого ссылка, тот может подбирать пароль, не спрашивая релей."
                                        : "Ссылка уйдёт без ключа, фразу скажете голосом."
                                on: root.pwOn
                                warnWhenOn: true
                                enabledState: root.draft && root.draft.relayPassword
                                onFlipped: root.pwOn = !root.pwOn
                            }
                        }
                        Column {
                            visible: root.pwOn
                            width: parent.width
                            spacing: 6
                            FerryKit.SmallLabel { text: "ФРАЗА" }
                            FerryCard.Input {
                                id: pwIn
                                placeholder: "пять слов, которые вы скажете голосом"
                                border.color: root.pwText === "" ? FerryStyle.line200
                                              : root.pwOk ? FerryStyle.ok : FerryStyle.warn
                            }
                            FerryCard.Caption {
                                width: parent.width
                                color: root.pwText === "" ? FerryStyle.ink300 : root.pwOk ? FerryStyle.okText : FerryStyle.warnText
                                text: root.pwText === ""
                                      ? "Нужно не меньше 5 слов и 20 знаков. Регистр и «ё» не важны."
                                      : root.pwOk
                                        ? root.pwWords + " " + FerryStyle.plural(root.pwWords, "слово", "слова", "слов") + ", "
                                          + root.pwChars + " " + FerryStyle.plural(root.pwChars, "знак", "знака", "знаков")
                                          + " — годится. Скажите её голосом, не пишите в этот чат."
                                        : "Пока " + root.pwWords + " " + FerryStyle.plural(root.pwWords, "слово", "слова", "слов")
                                          + " и " + root.pwChars + " " + FerryStyle.plural(root.pwChars, "знак", "знака", "знаков")
                                          + ". Нужно не меньше 5 слов и 20 знаков."
                            }
                        }
                    }

                    // ---- релей ----
                    Column {
                        width: parent.width - 32
                        spacing: 10
                        Item {
                            width: parent.width
                            height: 36
                            Rectangle { width: parent.width; height: 1; color: FerryStyle.line100 }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: FerryStyle.line100 }
                            FerryCard.Caption {
                                anchors.verticalCenter: parent.verticalCenter
                                color: FerryStyle.ink200
                                text: "релей"
                            }
                            Row {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                FerryCard.Mono {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.draft && root.draft.relay !== ""
                                          ? root.draft.relay.replace(/^https?:\/\//, "") : "не задан"
                                    color: root.draft && root.draft.relay !== "" ? FerryStyle.ink100 : FerryStyle.warnText
                                }
                                Rectangle {
                                    visible: root.draft && root.draft.relaySource !== ""
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: srcText.implicitWidth + 12
                                    height: 20
                                    color: "transparent"
                                    border.width: 1
                                    border.color: FerryStyle.line200
                                    Text {
                                        id: srcText
                                        anchors.centerIn: parent
                                        text: root.draft && root.draft.relaySource === "server" ? "от сервера" : "свой"
                                        color: FerryStyle.ink200
                                        font.family: FerryStyle.mono
                                        font.pixelSize: 11
                                    }
                                }
                                FerryKit.TextAction {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "сменить"
                                    onClicked: root.relayEditing = !root.relayEditing
                                }
                            }
                        }
                        Row {
                            visible: root.relayEditing || (root.draft && root.draft.relay === "")
                            width: parent.width
                            spacing: 10
                            FerryCard.Input {
                                id: relayIn
                                width: parent.width - relayOk.width - 10
                                placeholder: "https://ferry.example.com"
                                onAccepted: relayOk.clicked()
                            }
                            FerryCard.Button {
                                id: relayOk
                                small: true
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Взять"
                                onClicked: {
                                    if (root.draft && root.draft.setRelay(relayIn.text))
                                        root.relayEditing = false
                                    else
                                        relayBad.visible = true
                                }
                            }
                        }
                        FerryCard.Caption {
                            id: relayBad
                            visible: false
                            color: FerryStyle.dangerText
                            text: "Нужен адрес вида https://имя[:порт]."
                        }
                        FerryCard.Caption {
                            visible: root.draft && root.draft.relayStatus === "checking"
                            text: "Спрашиваем релей, что он умеет…"
                        }
                        FerryCard.Caption {
                            visible: root.draft && root.draft.relayStatus === "bad"
                            width: parent.width
                            color: FerryStyle.dangerText
                            text: "Релей не ответил как Ferry — проверьте адрес."
                        }
                        FerryCard.Caption {
                            width: parent.width
                            color: root.e2e ? FerryStyle.ink300 : FerryStyle.warnText
                            text: root.e2e
                                  ? "Комната под сквозным шифрованием: ссылку прочтут только участники."
                                  : root.pwOn
                                    ? "В комнате нет сквозного шифрования, но ключа в ссылке не будет — фразу скажете голосом."
                                    : "В комнате нет сквозного шифрования: ссылку с ключом прочтёт и сервер MeetUp. "
                                      + "Поэтому «спрашивать, кого пускать» включено заранее."
                        }
                    }

                    // ---- кнопки ----
                    Column {
                        width: parent.width - 32
                        spacing: 8
                        Row {
                            spacing: 10
                            FerryCard.Button {
                                primary: true
                                enabledState: root.ready
                                width: Math.max(implicitWidth, sheet.width - 32 - 2 - cancelBtn.width - 10)
                                text: root.draft && root.draft.sending ? "Готовим…" : "Отправить в чат"
                                onClicked: root.send()
                            }
                            FerryCard.Button {
                                id: cancelBtn
                                text: "Отмена"
                                onClicked: root.close()
                            }
                        }
                        FerryCard.Caption {
                            text: "Ссылка появится в чате сразу, хеши досчитаются в фоне."
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: addFiles
        title: "Файлы через Ferry"
        fileMode: FileDialog.OpenFiles
        onAccepted: if (root.draft) root.draft.add(selectedFiles)
    }
    FolderDialog {
        id: addFolder
        title: "Папка через Ferry"
        onAccepted: if (root.draft) root.draft.add([selectedFolder])
    }
}
