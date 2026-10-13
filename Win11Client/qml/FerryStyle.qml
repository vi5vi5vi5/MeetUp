pragma Singleton
import QtQuick

// Язык карточки Ferry: сталь и сигнальный оранжевый, всегда тёмный — в
// обеих темах MeetUp карточка выглядит вставленным предметом, как плеер.
// Значения — тёмная тема из ds/ferry/tokens.css макета Ferry.
QtObject {
    readonly property color surface000: "#0b1015"
    readonly property color surface100: "#121a21"
    readonly property color surface200: "#19242d"
    readonly property color surface300: "#22303a"
    readonly property color line100: "#263440"
    readonly property color line200: "#5a7181"
    readonly property color ink100: "#e8eef2"
    readonly property color ink200: "#a8b7c2"
    readonly property color ink300: "#7e93a2"
    readonly property color inkOnSignal: "#16181a"
    readonly property color signal: "#ff6a1f"
    readonly property color signalHover: "#ff7d3b"
    readonly property color signalText: "#ff8c47"
    readonly property color seed: "#35b3a0"
    readonly property color seedText: "#5ecdbb"
    readonly property color ok: "#3fae6a"
    readonly property color okText: "#62c98a"
    readonly property color warn: "#e5a32a"
    readonly property color warnText: "#f0b95a"
    readonly property color danger: "#c8332e"
    readonly property color dangerText: "#ff8a80"
    readonly property color wait: "#6f8697"
    readonly property color chunkNone: "#1b2730"
    readonly property color chunkInflight: "#3d505c"
    readonly property color chunkHave: "#5a7382"
    readonly property color chunkPeer: "#2ba192"
    readonly property color focus: "#6db9ff"

    // IBM Plex — шрифты Ferry (OFL, resources/fonts/IBMPlex-OFL.txt). Sans —
    // переменный по насыщенности, Mono — тремя файлами.
    property FontLoader _sans: FontLoader { source: Qt.resolvedUrl("../resources/fonts/IBMPlexSans.ttf") }
    property FontLoader _mono: FontLoader { source: Qt.resolvedUrl("../resources/fonts/IBMPlexMono-Regular.ttf") }
    property FontLoader _monoMedium: FontLoader { source: Qt.resolvedUrl("../resources/fonts/IBMPlexMono-Medium.ttf") }
    property FontLoader _monoSemi: FontLoader { source: Qt.resolvedUrl("../resources/fonts/IBMPlexMono-SemiBold.ttf") }
    readonly property string sans: _sans.status === FontLoader.Ready ? _sans.name : "Segoe UI"
    readonly property string mono: _mono.status === FontLoader.Ready ? _mono.name : "Consolas"

    // ---- Числа по-русски: запятая, неразрывный пробел между тысячами ----
    function num(x, digits) {
        const s = Number(x).toFixed(digits === undefined ? 0 : digits)
        const parts = s.split(".")
        parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, " ")
        return parts.join(",")
    }
    // Размер: [число, единица] — единицу карточка красит приглушённо.
    function size(bytes) {
        const u = ["Б", "КиБ", "МиБ", "ГиБ", "ТиБ"]
        let v = Number(bytes), i = 0
        while (v >= 1024 && i < u.length - 1) { v /= 1024; i++ }
        return [num(v, i === 0 || v >= 100 ? 0 : 1), u[i]]
    }
    function plural(n, one, few, many) {
        const m10 = n % 10, m100 = n % 100
        if (m10 === 1 && m100 !== 11) return one
        if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return few
        return many
    }
    function eta(ms) {
        if (ms < 0) return "—"
        const s = Math.round(ms / 1000)
        if (s >= 3600) return Math.floor(s / 3600) + " ч " + String(Math.floor(s % 3600 / 60)).padStart(2, "0") + " мин"
        if (s >= 60) return Math.floor(s / 60) + " мин " + String(s % 60).padStart(2, "0") + " с"
        return s + " с"
    }
}
