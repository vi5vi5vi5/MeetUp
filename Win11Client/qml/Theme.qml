pragma Singleton
import QtQuick

// MeetUp design tokens — "Prism Minimal".
// Values mirror the reference web client (Server/web/assets/theme.css) 1:1.
// Dark is the default; `dark = false` flips the whole palette live.
QtObject {
    id: theme

    property bool dark: true

    // ---- Surfaces ----
    readonly property color bg:           dark ? "#0e0e10" : "#f4f3ee"
    readonly property color surface:      dark ? "#161619" : "#ffffff"
    readonly property color surface2:     dark ? "#1e1e22" : "#eceadf"
    readonly property color surface3:     dark ? "#26262b" : "#e4e2d8"
    readonly property color border:       dark ? "#26262b" : "#e4e2d8"
    readonly property color borderStrong: dark ? "#33333a" : "#dedcd2"

    // ---- Text ----
    readonly property color text:      dark ? "#f2f2f4" : "#16161a"
    readonly property color textMuted: dark ? "#9a9aa2" : "#8a8a80"
    readonly property color textFaint: dark ? "#6a6a72" : "#a8a89e"

    // ---- Accent (acid lime — the only chroma in the system) ----
    readonly property color accent:    "#c6ff3d"
    readonly property color accentInk: dark ? "#c6ff3d" : "#5b8c00"
    readonly property color accentFg:  dark ? "#10120a" : "#16161a"

    // ---- Semantic ----
    readonly property color danger:   dark ? "#ff5470" : "#e0225b"
    readonly property color dangerFg: "#ffffff"
    readonly property color live:     dark ? "#c6ff3d" : "#7ab80a"

    // ---- Monochrome video-tile ramps ----
    readonly property color tileFrom:     dark ? "#2b2b32" : "#f4f2ea"
    readonly property color tileTo:       dark ? "#17171b" : "#e7e5da"
    readonly property color tileSelfFrom: dark ? "#3a3a42" : "#ffffff"
    readonly property color tileSelfTo:   dark ? "#17171b" : "#eceadf"

    readonly property color scrimChip:   dark ? Qt.rgba(0, 0, 0, 0.5) : Qt.rgba(1, 1, 1, 0.85)
    readonly property color shadowColor: dark ? Qt.rgba(0, 0, 0, 0.5) : Qt.rgba(0.23, 0.19, 0.08, 0.14)

    // ---- Corner radii ----
    readonly property int radiusCard: 16
    readonly property int radiusLg:   14
    readonly property int radiusMd:   12
    readonly property int radiusSm:   10
    readonly property int radiusXs:   7
    readonly property int radiusPill: 999

    // ---- Spacing ----
    readonly property int gapGrid:   10
    readonly property int gapInline: 8
    readonly property int padCard:   24
    readonly property int padPanel:  20
    readonly property int padStage:  26

    // ---- Type scale ----
    readonly property int textDisplay: 34
    readonly property int text2xl:     26
    readonly property int textXl:      20
    readonly property int textLg:      16
    readonly property int textMd:      14
    readonly property int textSm:      13
    readonly property int textXs:      12
    readonly property int text2xs:     11

    // ---- Motion (ms) ----
    readonly property int durFast: 150
    readonly property int durMed:  240

    // ---- Fonts (bundled TTF, graceful fallback to Segoe UI) ----
    property FontLoader _display: FontLoader { source: Qt.resolvedUrl("../resources/fonts/Unbounded.ttf") }
    property FontLoader _ui:      FontLoader { source: Qt.resolvedUrl("../resources/fonts/Manrope.ttf") }
    property FontLoader _label:   FontLoader { source: Qt.resolvedUrl("../resources/fonts/SpaceGrotesk.ttf") }

    readonly property string displayFont: _display.status === FontLoader.Ready ? _display.name : "Segoe UI"
    readonly property string uiFont:      _ui.status === FontLoader.Ready ? _ui.name : "Segoe UI"
    readonly property string labelFont:   _label.status === FontLoader.Ready ? _label.name : "Segoe UI"
    readonly property string monoFont:    "Consolas"

    function toggle() { dark = !dark }
}
