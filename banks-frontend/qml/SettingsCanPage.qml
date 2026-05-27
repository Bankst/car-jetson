import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property bool canAvailable: false
    property string canInterface: "can0"
    property string bitrate: ""
    property string busState: ""
    property string txFrames: ""
    property string rxFrames: ""
    property string errorCount: ""

    // CAN stats read via SystemInfo C++ backend (avoids XHR local file restriction)
    // For now, check if interface exists at startup only. Full CAN monitoring
    // requires a C++ backend like SystemInfo.
    Component.onCompleted: {
        // Use SystemInfo-style approach: can't read /sys from QML.
        // CAN interface detection deferred to C++ backend on Jetson.
        root.canAvailable = false
    }

    // --- CAN not available ---
    Text {
        visible: !root.canAvailable
        anchors.centerIn: parent
        text: "CAN interface not available"
        color: "#555"; font.pixelSize: 18
    }

    // --- CAN available ---
    Flickable {
        visible: root.canAvailable
        anchors.fill: parent
        anchors.margins: 24
        contentHeight: col.height
        clip: true

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 20

            Text { text: "CAN Interface"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            GridLayout {
                columns: 2; columnSpacing: 24; rowSpacing: 12; Layout.fillWidth: true

                Text { text: "Interface"; color: "#aaa"; font.pixelSize: 14 }
                Text { text: root.canInterface; color: "#888"; font.pixelSize: 14 }

                Text { text: "Bus State"; color: "#aaa"; font.pixelSize: 14 }
                Text {
                    text: root.busState
                    color: root.busState === "Active" ? "#44cc44" : "#cc4444"
                    font.pixelSize: 14; font.weight: Font.DemiBold
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            Text { text: "Frame Counters"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            GridLayout {
                columns: 2; columnSpacing: 24; rowSpacing: 12; Layout.fillWidth: true

                Text { text: "TX Frames"; color: "#aaa"; font.pixelSize: 14 }
                Text { text: root.txFrames || "—"; color: "#888"; font.pixelSize: 14 }

                Text { text: "RX Frames"; color: "#aaa"; font.pixelSize: 14 }
                Text { text: root.rxFrames || "—"; color: "#888"; font.pixelSize: 14 }

                Text { text: "Errors"; color: "#aaa"; font.pixelSize: 14 }
                Text {
                    text: root.errorCount || "—"
                    color: parseInt(root.errorCount) > 0 ? "#cc4444" : "#888"
                    font.pixelSize: 14
                }
            }
        }
    }
}
