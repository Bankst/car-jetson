import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property var viz: Window.window ? Window.window.visualizer : null
    property var spec: Window.window ? Window.window.spectrumWidget : null

    Flickable {
        anchors.fill: parent
        anchors.margins: 24
        contentHeight: col.height
        clip: true

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 20

            // --- Current Preset ---
            Text { text: "Visualizer"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            GridLayout {
                columns: 2; columnSpacing: 24; rowSpacing: 12; Layout.fillWidth: true

                Text { text: "Current Preset"; color: "#aaa"; font.pixelSize: 14 }
                Text {
                    text: root.viz ? root.viz.currentPreset : "—"
                    color: "#888"; font.pixelSize: 14
                    Layout.fillWidth: true; elide: Text.ElideMiddle
                }

                Text { text: "Favorites"; color: "#aaa"; font.pixelSize: 14 }
                Text {
                    text: root.viz ? root.viz.favoritesCount + " presets" : "—"
                    color: "#888"; font.pixelSize: 14
                }
            }

            RowLayout {
                spacing: 12
                Button { text: "Prev"; flat: true; enabled: root.viz && root.viz.canPrev; onClicked: root.viz.prev() }
                Button { text: "Next"; flat: true; enabled: root.viz && root.viz.canNext; onClicked: root.viz.next() }
                Button {
                    text: root.viz && root.viz.locked ? "Unlock" : "Lock"
                    flat: true
                    onClicked: if (root.viz) root.viz.toggleLock()
                }
                Button {
                    text: root.viz && root.viz.currentIsFavorite ? "Unfavorite" : "Favorite"
                    flat: true
                    onClicked: if (root.viz) root.viz.toggleFavorite()
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Modes ---
            Text { text: "Modes"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Text { text: "Favorites Only"; color: "#aaa"; font.pixelSize: 14 }
                Switch {
                    checked: root.viz ? root.viz.favoritesMode : false
                    onToggled: if (root.viz) root.viz.favoritesMode = checked
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Timing & Sensitivity ---
            Text { text: "Timing & Sensitivity"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Text { text: "Beat Sensitivity"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 120 }
                Slider {
                    id: sensitivitySlider
                    Layout.fillWidth: true
                    from: 0.0; to: 2.0; stepSize: 0.1
                    value: root.viz ? root.viz.sensitivity : 1.0
                    onMoved: if (root.viz) root.viz.sensitivity = value
                }
                Text { text: sensitivitySlider.value.toFixed(1); color: "#888"; font.pixelSize: 12; Layout.preferredWidth: 30 }
            }

            RowLayout {
                spacing: 12
                Text { text: "Preset Duration"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 120 }
                Slider {
                    id: durationSlider
                    Layout.fillWidth: true
                    from: 5; to: 120; stepSize: 5
                    value: root.viz ? root.viz.presetDuration : 30
                    onMoved: if (root.viz) root.viz.presetDuration = value
                }
                Text { text: Math.round(durationSlider.value) + "s"; color: "#888"; font.pixelSize: 12; Layout.preferredWidth: 36 }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Spectrum Analyzer ---
            Text { text: "Spectrum Analyzer"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Text { text: "Bands"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 60 }
                Slider {
                    id: bandSlider
                    Layout.fillWidth: true
                    from: 8; to: 128; stepSize: 8
                    value: root.spec ? root.spec.bandCount : 32
                    onMoved: if (root.spec) root.spec.bandCount = value
                }
                Text { text: Math.round(bandSlider.value); color: "#888"; font.pixelSize: 12; Layout.preferredWidth: 30 }
            }
        }
    }
}
