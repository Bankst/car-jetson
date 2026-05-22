import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BanksFrontend

Item {
    id: root

    Visualizer {
        id: viz
        anchors.fill: parent
    }

    // Preset name pill — upper right, fades after preset change.
    Rectangle {
        id: presetPill
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 16
        anchors.rightMargin: 16
        radius: 4
        color: "#80000000"
        border.color: "#40ffffff"
        border.width: 1
        opacity: 0
        width:  presetText.implicitWidth + 16
        height: presetText.implicitHeight + 8

        Text {
            id: presetText
            anchors.centerIn: parent
            text: viz.currentPreset
            color: "#ffffff"
            font.pixelSize: 12
            font.family: "monospace"
        }

        Behavior on opacity {
            NumberAnimation { duration: 600; easing.type: Easing.OutCubic }
        }
    }

    Timer {
        id: hidePresetPill
        interval: 2500
        onTriggered: presetPill.opacity = 0
    }

    Connections {
        target: viz
        function onCurrentPresetChanged() {
            presetPill.opacity = 1.0
            hidePresetPill.restart()
        }
    }

    // Floating controls
    Row {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 64
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 16

        Button {
            text: "Prev"
            enabled: viz.canPrev
            opacity: enabled ? 1.0 : 0.4
            onClicked: viz.prev()
        }
        Button {
            text: "Next"
            enabled: viz.canNext
            opacity: enabled ? 1.0 : 0.4
            onClicked: viz.next()
        }

        // Lock toggle — locks current preset (no auto-progress)
        Rectangle {
            id: lockBtn
            width: 40; height: 40; radius: 20
            color: viz.locked ? "#f44336" : "#33ffffff"
            border.color: "#80ffffff"; border.width: 1
            Text {
                anchors.centerIn: parent
                text: viz.locked ? "🔒" : "🔓"
                color: "#ffffff"; font.pixelSize: 18
            }
            MouseArea {
                anchors.fill: parent
                onClicked: viz.toggleLock()
            }
            Behavior on color { ColorAnimation { duration: 150 } }
        }

        // Shuffle indicator dot
        Rectangle {
            id: shuffleBtn
            property bool on: false
            width: 40; height: 40; radius: 20
            color: on ? "#4caf50" : "#33ffffff"
            border.color: "#80ffffff"; border.width: 1
            Text {
                anchors.centerIn: parent
                text: "S"; color: "#ffffff"; font.bold: true; font.pixelSize: 16
            }
            MouseArea {
                anchors.fill: parent
                onClicked: { shuffleBtn.on = !shuffleBtn.on; viz.shuffle(shuffleBtn.on) }
            }
            Behavior on color { ColorAnimation { duration: 150 } }
        }

        // Favorite star — fave/unfave current preset
        Rectangle {
            id: faveBtn
            width: 40; height: 40; radius: 20
            color: viz.currentIsFavorite ? "#ffc107" : "#33ffffff"
            border.color: "#80ffffff"; border.width: 1
            Text {
                anchors.centerIn: parent
                text: viz.currentIsFavorite ? "★" : "☆"
                color: "#ffffff"; font.pixelSize: 18
            }
            MouseArea {
                anchors.fill: parent
                onClicked: viz.toggleFavorite()
            }
            Behavior on color { ColorAnimation { duration: 150 } }
        }

        // Favorites-only mode toggle ("F")
        Rectangle {
            id: favesModeBtn
            width: 40; height: 40; radius: 20
            color: viz.favoritesMode ? "#ffc107" : "#33ffffff"
            border.color: "#80ffffff"; border.width: 1
            enabled: viz.favoritesCount > 0 || viz.favoritesMode
            opacity: enabled ? 1.0 : 0.4
            Text {
                anchors.centerIn: parent
                text: "F"; color: "#ffffff"; font.bold: true; font.pixelSize: 16
            }
            MouseArea {
                anchors.fill: parent
                onClicked: viz.favoritesMode = !viz.favoritesMode
            }
            Behavior on color { ColorAnimation { duration: 150 } }
        }
    }

    // Favorites count badge — bottom right
    Text {
        anchors.bottom: parent.bottom
        anchors.right:  parent.right
        anchors.margins: 8
        text: viz.favoritesCount + " ★"
        color: "#80ffffff"
        font.pixelSize: 11
        font.family: "monospace"
    }
}
