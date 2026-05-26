import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BanksFrontend

Item {
    id: root

    SystemInfo { id: sysInfo }

    Flickable {
        anchors.fill: parent
        anchors.margins: 24
        contentHeight: col.height
        clip: true

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 20

            Text { text: "System"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            GridLayout {
                columns: 2; columnSpacing: 24; rowSpacing: 12; Layout.fillWidth: true

                Text { text: "Hostname"; color: "#aaa"; font.pixelSize: 14 }
                Text { text: sysInfo.hostname || "—"; color: "#888"; font.pixelSize: 14 }

                Text { text: "Uptime"; color: "#aaa"; font.pixelSize: 14 }
                Text { text: sysInfo.uptime || "—"; color: "#888"; font.pixelSize: 14 }

                Text { text: "Kernel"; color: "#aaa"; font.pixelSize: 14 }
                Text { text: sysInfo.kernelVersion || "—"; color: "#888"; font.pixelSize: 14 }

                Text { text: "Software"; color: "#aaa"; font.pixelSize: 14 }
                Text { text: "banks-frontend v" + (Qt.application.version || "0.1.0"); color: "#888"; font.pixelSize: 14 }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Network ---
            Text { text: "Network"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            Repeater {
                model: sysInfo.netIfaces
                delegate: Text {
                    required property string modelData
                    text: modelData
                    color: modelData.indexOf("down") >= 0 ? "#555" : "#888"
                    font.pixelSize: 14
                }
            }

            Text {
                visible: sysInfo.netIfaces.length === 0
                text: "—"; color: "#555"; font.pixelSize: 14
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Thermals ---
            Text { text: "Thermals"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Text { text: "CPU"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 60 }
                Text {
                    text: sysInfo.cpuTemp ? sysInfo.cpuTemp + "°C" : "—"
                    color: {
                        var t = parseInt(sysInfo.cpuTemp)
                        return t >= 75 ? "#cc4444" : t >= 60 ? "#cccc44" : "#44cc44"
                    }
                    font.pixelSize: 14; font.weight: Font.DemiBold
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Memory ---
            Text { text: "Memory"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            ColumnLayout {
                spacing: 8; Layout.fillWidth: true
                Text { text: sysInfo.memText || "—"; color: "#aaa"; font.pixelSize: 14 }
                Rectangle {
                    visible: sysInfo.memUsage > 0
                    Layout.fillWidth: true; height: 16; radius: 4; color: "#222"
                    Rectangle {
                        width: parent.width * sysInfo.memUsage
                        height: parent.height; radius: 4
                        color: sysInfo.memUsage > 0.85 ? "#cc4444" : sysInfo.memUsage > 0.6 ? "#cccc44" : "#44cc44"
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Power ---
            Text { text: "Power"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Button {
                    text: "Reboot"; flat: true; enabled: false
                    ToolTip.visible: hovered; ToolTip.text: "Not available on dev host"
                }
                Button {
                    text: "Shutdown"; flat: true; enabled: false
                    ToolTip.visible: hovered; ToolTip.text: "Not available on dev host"
                }
            }
        }
    }
}
