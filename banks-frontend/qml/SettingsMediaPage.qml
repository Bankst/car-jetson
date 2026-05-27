import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BanksFrontend

Item {
    id: root

    property int audioSourceIndex: 1

    BluetoothManager { id: btMgr }

    Flickable {
        anchors.fill: parent
        anchors.margins: 24
        contentHeight: col.height
        clip: true

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 20

            // --- Bluetooth Adapter ---
            Text { text: "Bluetooth"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            GridLayout {
                columns: 3; columnSpacing: 12; rowSpacing: 10; Layout.fillWidth: true

                Text { text: "Power"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 100 }
                Switch {
                    id: powerSwitch
                    checked: btMgr.powered
                    onToggled: btMgr.powered = checked
                }
                Item { Layout.fillWidth: true }

                Text { text: "Discoverable"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 100 }
                Switch {
                    id: discoverableSwitch
                    checked: btMgr.discoverable
                    onToggled: btMgr.discoverable = checked
                    enabled: btMgr.powered
                }
                Item { Layout.fillWidth: true }

                Text { text: "Name"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 100 }
                TextField {
                    id: adapterNameField
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    text: btMgr.adapterName
                    color: "#ccc"; font.pixelSize: 13
                    enabled: btMgr.powered
                    background: Rectangle {
                        implicitHeight: 32
                        color: "#222"; border.color: adapterNameField.activeFocus ? "#558" : "#444"; radius: 4
                    }
                    onAccepted: btMgr.adapterName = text
                }

                Text { text: "Address"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 100 }
                Text {
                    text: btMgr.adapterAddress || "---"
                    color: "#666"; font.pixelSize: 13
                    Layout.columnSpan: 2; Layout.fillWidth: true
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Devices ---
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Devices"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Button {
                    text: btMgr.discovering ? "Stop Scan" : "Scan"
                    flat: true
                    enabled: btMgr.powered
                    onClicked: {
                        if (btMgr.discovering) btMgr.stopDiscovery()
                        else btMgr.startDiscovery()
                    }
                }
            }

            Text {
                visible: btMgr.discovering
                text: "Scanning..."
                color: "#668"; font.pixelSize: 12
            }

            Text {
                visible: btMgr.devices.length === 0
                text: btMgr.powered ? "No devices found" : "Bluetooth is off"
                color: "#555"; font.pixelSize: 13
            }

            Repeater {
                model: btMgr.devices
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    height: devRow.implicitHeight + 16
                    color: index % 2 === 0 ? "#1a1a1a" : "transparent"
                    radius: 4

                    RowLayout {
                        id: devRow
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 12

                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Text { text: modelData.name; color: "#ccc"; font.pixelSize: 14 }
                            Text { text: modelData.mac; color: "#555"; font.pixelSize: 11 }
                        }

                        Text {
                            text: modelData.connected ? "Connected" : modelData.paired ? "Paired" : "Available"
                            color: modelData.connected ? "#44cc44" : modelData.paired ? "#888" : "#557"
                            font.pixelSize: 12
                            Layout.preferredWidth: 80
                            horizontalAlignment: Text.AlignRight
                        }

                        Button {
                            text: modelData.connected ? "Disconnect" : "Connect"
                            flat: true
                            visible: modelData.paired
                            onClicked: {
                                if (modelData.connected)
                                    btMgr.disconnectDevice(modelData.mac)
                                else
                                    btMgr.connectDevice(modelData.mac)
                            }
                        }

                        Button {
                            text: "Remove"
                            flat: true
                            visible: modelData.paired
                            onClicked: btMgr.removePairing(modelData.mac)
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Audio Source ---
            Text { text: "Audio Source"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Text { text: "Source"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 100 }
                ComboBox {
                    id: sourceCombo
                    Layout.fillWidth: true
                    model: ["Android Auto", "Bluetooth A2DP", "USB Audio", "AUX"]
                    currentIndex: root.audioSourceIndex
                    onActivated: function(index) { root.audioSourceIndex = index }

                    background: Rectangle {
                        implicitWidth: 200; implicitHeight: 36
                        color: "#222"; border.color: "#444"; radius: 4
                    }
                    contentItem: Text {
                        leftPadding: 8
                        text: sourceCombo.displayText
                        color: "#ccc"; font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                    }
                    popup: Popup {
                        y: sourceCombo.height
                        width: sourceCombo.width
                        implicitHeight: contentItem.implicitHeight
                        padding: 1
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: sourceCombo.popup.visible ? sourceCombo.delegateModel : null
                            ScrollIndicator.vertical: ScrollIndicator {}
                        }
                        background: Rectangle { color: "#222"; border.color: "#444"; radius: 4 }
                    }
                    delegate: ItemDelegate {
                        width: sourceCombo.width
                        contentItem: Text {
                            text: modelData
                            color: sourceCombo.highlightedIndex === index ? "#fff" : "#aaa"
                            font.pixelSize: 13
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: sourceCombo.highlightedIndex === index ? "#336" : "transparent"
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Volume Normalization ---
            RowLayout {
                spacing: 12
                Text { text: "Volume Normalization"; color: "#aaa"; font.pixelSize: 14 }
                Switch {
                    id: normSwitch
                    checked: false
                }
            }

            Text { text: "Reduces volume jumps between tracks"; color: "#555"; font.pixelSize: 11 }
        }
    }
}
