import QtQuick
import MeetUp

// One chat entry. `self` right-aligns and fills the bubble with accent;
// others are inset surface bubbles on the left with an author line above.
Item {
    id: root

    property string author: ""
    property string text: ""
    property string time: ""
    property bool self: false
    // Сообщение зашифровано ключом, которого у нас нет. Показать шифротекст
    // было бы враньём, промолчать — потерей сообщения: показываем, что оно
    // было, и почему его не видно.
    property bool locked: false

    readonly property real _maxContent: width * 0.82 - 24
    readonly property color _linkColor: root.self ? Theme.accentFg : Theme.accent

    implicitHeight: col.implicitHeight
    // width supplied by the containing list

    // Текст сообщения как размеченный документ: адреса становятся ссылками,
    // переводы строк — переводами строк. Всё пришедшее по сети СНАЧАЛА
    // экранируется: сообщение пишет другой человек, и «<b>» в нём должно
    // остаться видимым текстом, а не разметкой.
    function _markup(raw) {
        const esc = String(raw).replace(/&/g, "&amp;")
                               .replace(/</g, "&lt;")
                               .replace(/>/g, "&gt;")
        // Цвет ссылки в разметку надо отдать строкой: "#aarrggbb" QTextDocument
        // не понимает, поэтому альфу отбрасываем (она у нас всё равно 1).
        let col = String(root._linkColor)
        if (col.length === 9) col = "#" + col.substring(3)

        return esc.replace(/((?:https?:\/\/|www\.)[^\s<>"']+)/gi, function (m) {
            // Точка в конце — конец предложения, а не часть адреса. Скобку
            // забираем себе, только если её внутри адреса не открывали:
            // «(см. example.com/a)» и «example.com/wiki/Foo_(bar)» — разные
            // случаи, и оба встречаются.
            let url = m, tail = ""
            while (url.length > 0) {
                const last = url.charAt(url.length - 1)
                if (".,;:!?»".indexOf(last) >= 0
                    || (last === ")" && url.indexOf("(") < 0)) {
                    tail = last + tail
                    url = url.slice(0, -1)
                    continue
                }
                break
            }
            if (url.length === 0) return m
            const href = /^www\./i.test(url) ? "https://" + url : url
            return "<a href=\"" + href + "\" style=\"color:" + col + "\">"
                 + url + "</a>" + tail
        }).replace(/\n/g, "<br>")
    }

    Column {
        id: col
        width: parent.width
        spacing: 3

        Row {
            visible: !root.self && (root.author !== "" || root.time !== "")
            leftPadding: 4
            spacing: 6
            Text {
                text: root.author
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.text2xs
                font.weight: Font.DemiBold
            }
            Text {
                visible: root.time !== ""
                text: root.time
                color: Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.text2xs
            }
        }

        // Заглушка вместо пузыря: замок и объяснение. Пунктирная рамка вместо
        // заливки — чтобы её не приняли за пришедший текст.
        Rectangle {
            visible: root.locked
            x: root.self ? parent.width - width : 0
            width: lockRow.implicitWidth + 24
            height: lockRow.implicitHeight + 16
            color: "transparent"
            radius: Theme.radiusSm
            border.width: 1
            border.color: Theme.border

            Row {
                id: lockRow
                anchors.centerIn: parent
                spacing: 8
                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "lock"
                    size: 13
                    color: Theme.textFaint
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    // Две разные ситуации и два разных действия — см. LockPlate.
                    text: Crypto.active ? "Зашифровано другим ключом"
                                        : "Зашифровано, нужен ключ"
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textSm
                    font.italic: true
                }
            }
        }

        Rectangle {
            id: bubble
            visible: !root.locked
            x: root.self ? parent.width - width : 0
            width: msg.width + 24
            height: msg.implicitHeight + 16
            color: root.self ? Theme.accent : Theme.surface2
            radius: Theme.radiusSm
            bottomRightRadius: root.self ? 3 : Theme.radiusSm
            bottomLeftRadius: root.self ? Theme.radiusSm : 3

            // Невидимая мерка. Ширину пузыря нельзя брать у самого TextEdit:
            // при переносе строк его implicitWidth считается уже ПО ПЕРЕНЕСЁННОМУ
            // тексту, и привязка ширины к нему сходится в петлю, ужимая абзац в
            // колонку по слову. Обычный Text без переносов честно меряет строку.
            Text {
                id: probe
                visible: false
                text: root.text
                font: msg.font
            }

            // TextEdit, а не Text: людям нужно выделять и копировать сказанное —
            // адрес комнаты, номер, чужое имя. Только чтение, но с курсором и
            // выделением; ссылки открываются в браузере.
            TextEdit {
                id: msg
                x: 12; y: 8
                width: Math.min(probe.implicitWidth, root._maxContent)
                text: root._markup(root.text)
                textFormat: TextEdit.RichText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: root.self ? Theme.accentFg : Theme.text
                // Выделение — обмен цветами пузыря: на лаймовом своём фоне
                // подсветка лаймом была бы невидимой, поэтому там наоборот.
                selectionColor: root.self ? Theme.accentFg : Theme.accent
                selectedTextColor: root.self ? Theme.accent : Theme.accentFg
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm

                onLinkActivated: function (link) { Qt.openUrlExternally(link) }

                // Курсор-рука над ссылкой. Кнопок не принимаем: нажатия должны
                // доставаться самому TextEdit, иначе пропадёт выделение.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    cursorShape: msg.hoveredLink !== "" ? Qt.PointingHandCursor
                                                        : Qt.IBeamCursor
                }
            }
        }
    }
}
