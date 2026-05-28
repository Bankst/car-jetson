import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BanksFrontend

Item {
    id: root

    CanInfo {
        id: can
        iface: "can0"
    }

    // --- CAN not available ---
    Text {
        visible: !can.available
        anchors.centerIn: parent
        text: "CAN interface not available"
        color: "#555"; font.pixelSize: 18
    }

    // --- CAN available ---
    Flickable {
        visible: can.available
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
                Text { text: can.iface; color: "#888"; font.pixelSize: 14 }

                Text { text: "Bus State"; color: "#aaa"; font.pixelSize: 14 }
                Text {
                    text: can.state
                    color: can.state.indexOf("Active") === 0 ? "#44cc44" : "#cc4444"
                    font.pixelSize: 14; font.weight: Font.DemiBold
                }

                Text { text: "Bitrate"; color: "#aaa"; font.pixelSize: 14 }
                Text {
                    text: can.bitrate > 0 ? (can.bitrate + " bps") : "—"
                    color: "#888"; font.pixelSize: 14
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            Text { text: "Frame Counters"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            GridLayout {
                columns: 2; columnSpacing: 24; rowSpacing: 12; Layout.fillWidth: true

                Text { text: "TX Frames"; color: "#aaa"; font.pixelSize: 14 }
                Text { text: can.txPackets; color: "#888"; font.pixelSize: 14 }

                Text { text: "RX Frames"; color: "#aaa"; font.pixelSize: 14 }
                Text { text: can.rxPackets; color: "#888"; font.pixelSize: 14 }

                Text { text: "TX Errors"; color: "#aaa"; font.pixelSize: 14 }
                Text {
                    text: can.txErrors
                    color: can.txErrors > 0 ? "#cc4444" : "#888"
                    font.pixelSize: 14
                }

                Text { text: "RX Errors"; color: "#aaa"; font.pixelSize: 14 }
                Text {
                    text: can.rxErrors
                    color: can.rxErrors > 0 ? "#cc4444" : "#888"
                    font.pixelSize: 14
                }

                Text { text: "TX Err Counter"; color: "#aaa"; font.pixelSize: 14 }
                Text { text: can.txErrCounter; color: "#888"; font.pixelSize: 14 }

                Text { text: "RX Err Counter"; color: "#aaa"; font.pixelSize: 14 }
                Text { text: can.rxErrCounter; color: "#888"; font.pixelSize: 14 }
            }
        }
    }
}
