import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property int audioSourceIndex: 1

    ListModel {
        id: btDevices
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 24
        contentHeight: col.height
        clip: true

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 20

            // --- Bluetooth Devices ---
            Text { text: "Bluetooth Devices"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            Text {
                visible: btDevices.count === 0
                text: "No devices — scan to discover"
                color: "#555"; font.pixelSize: 13
            }

            Repeater {
                model: btDevices
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Text { text: model.name; color: "#ccc"; font.pixelSize: 14 }
                        Text { text: model.mac; color: "#666"; font.pixelSize: 11 }
                    }

                    Text {
                        text: model.connected ? "Connected" : "Paired"
                        color: model.connected ? "#44cc44" : "#888"
                        font.pixelSize: 12
                        Layout.preferredWidth: 80
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            Button {
                text: "Scan"
                flat: true
            }

            Text { text: "Requires Qt6::Bluetooth backend"; color: "#555"; font.pixelSize: 11 }

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
