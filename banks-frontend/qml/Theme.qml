pragma Singleton

import QtQuick
import QtCore

// App-wide UI scale + sizing constants.
//
// `s` is a live multiplier: changing it (e.g. via the Display settings slider)
// relayouts every binding that reads it, with no restart. It is persisted via
// the Settings alias below and restored at boot.
//
// MIGRATION NOTE (incremental literal sweep in progress):
//   The launch env still sets QT_SCALE_FACTOR=1.5, which scales ALL un-migrated
//   QML literals. So `s` defaults to 1.0 here — migrated elements (Theme.*)
//   render unchanged while the env scale still carries the bulk of the UI.
//   FINAL CUTOVER (after all literals migrated to Theme.*):
//     - remove QT_SCALE_FACTOR from the weston/launch env
//     - change the default below from 1.0 to 1.5
QtObject {
    id: theme

    // --- live scale knob ---
    property real s: 1.0

    // --- resolved sizing constants (recompute when `s` changes) ---
    readonly property int  navH:    Math.round(48 * s)   // footer nav height
    readonly property int  rowH:    Math.round(40 * s)   // list/settings row
    readonly property int  pad:     Math.round(8  * s)   // base padding unit
    readonly property int  padLg:   Math.round(24 * s)   // page margins
    readonly property int  gap:     Math.round(12 * s)   // inter-control spacing
    readonly property int  radius:  Math.round(4  * s)

    // --- resolved font sizes ---
    readonly property int  fontXs:  Math.round(11 * s)
    readonly property int  fontSm:  Math.round(14 * s)
    readonly property int  fontMd:  Math.round(20 * s)
    readonly property int  fontLg:  Math.round(24 * s)

    // Escape hatch for one-off literals not worth a named constant.
    function dp(v) { return Math.round(v * s) }

    // Persistence: two-way. Boot reads stored "ui/scale" into `s`; any change
    // to `s` is written back. Default file = platform app-config location.
    property Settings _store: Settings {
        category: "ui"
        property alias scale: theme.s
    }
}
