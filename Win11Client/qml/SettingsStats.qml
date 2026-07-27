import QtQuick
import MeetUp

// Раздел «Диагностика». Задержка здесь настоящая — её уже меряет SignalingClient;
// остальные числа движки тоже считают и раз в пять секунд пишут в журнал,
// осталось выставить их свойствами. Смысл раздела прикладной: следующий
// разговор про «звук отстаёт» начнётся не с просьбы прислать лог.
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    component Metric: Rectangle {
        id: cell
        property string caption: ""
        property string value: "—"
        property string unit: ""

        height: 62
        radius: Theme.radiusMd
        color: Theme.surface2
        border.width: 1
        border.color: Theme.border

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

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
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Потери пакетов"
            opacity: 0.62
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Отправка · камера"
            opacity: 0.62
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Отправка · экран"
            opacity: 0.62
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Приём"
            opacity: 0.62
        }
        Metric {
            width: (parent.width - 10) / 2
            caption: "Рассинхрон губ"
            opacity: 0.62
        }
    }

    Field {
        width: parent.width
        label: "Отчёт для разбора"
        soon: true
        hint: "Один текст со всеми числами и версиями — вместо просьбы «пришлите лог»."
        Row {
            width: parent.width
            spacing: 8
            enabled: false
            opacity: 0.62

            AppButton {
                width: (parent.width - 8) / 2
                text: "Скопировать отчёт"
                variant: "secondary"
                size: "sm"
            }
            AppButton {
                width: (parent.width - 8) / 2
                text: "Открыть журнал"
                variant: "ghost"
                size: "sm"
            }
        }
    }
}
