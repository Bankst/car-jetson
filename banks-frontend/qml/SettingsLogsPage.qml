import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BanksFrontend

Item {
    id: root

    // LogCapture is a singleton registered in main.cpp — no instantiation needed.
    // Referenced directly as "LogCapture" throughout.

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        // --- Toolbar ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text { text: "Logs"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            Item { Layout.fillWidth: true }

            // Filter buttons
            Repeater {
                model: [
                    { label: "All",   level: 0 },
                    { label: "Info",  level: 1 },
                    { label: "Warn",  level: 2 },
                    { label: "Error", level: 3 }
                ]
                delegate: Button {
                    required property var modelData
                    text: modelData.label
                    flat: true
                    checkable: true
                    checked: LogCapture.filterLevel === modelData.level
                    onClicked: LogCapture.filterLevel = modelData.level
                    contentItem: Text {
                        text: parent.text
                        color: parent.checked ? "#fff" : "#666"
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                    }
                    background: Rectangle {
                        color: parent.checked ? "#444" : "#222"
                        radius: 4
                        implicitWidth: 50
                        implicitHeight: 28
                    }
                }
            }

            Rectangle { width: 1; height: 24; color: "#333" }

            // Auto-scroll toggle
            Button {
                text: "Auto-scroll"
                flat: true
                checkable: true
                checked: LogCapture.autoScroll
                onClicked: LogCapture.autoScroll = checked
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? "#44cc44" : "#666"
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: parent.checked ? "#1a331a" : "#222"
                    radius: 4
                    implicitWidth: 80
                    implicitHeight: 28
                }
            }

            Rectangle { width: 1; height: 24; color: "#333" }

            Button {
                text: "Copy"
                flat: true
                onClicked: LogCapture.copyToClipboard()
                contentItem: Text {
                    text: parent.text; color: "#aaa"; font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: parent.down ? "#444" : "#222"; radius: 4
                    implicitWidth: 50; implicitHeight: 28
                }
            }

            Button {
                text: "Clear"
                flat: true
                onClicked: LogCapture.clear()
                contentItem: Text {
                    text: parent.text; color: "#cc4444"; font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: parent.down ? "#442222" : "#222"; radius: 4
                    implicitWidth: 50; implicitHeight: 28
                }
            }
        }

        // --- Line count ---
        Text {
            text: logView.count + " lines shown (" + LogCapture.totalCount + " total)"
            color: "#555"; font.pixelSize: 11
        }

        // --- Log output ---
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#111"
            radius: 4

            ListView {
                id: logView
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: LogCapture.messages
                reuseItems: true

                delegate: Text {
                    required property string modelData
                    required property int index
                    width: logView.width
                    text: modelData
                    color: {
                        if (modelData.indexOf("[ERR]") >= 0 || modelData.indexOf("[FTL]") >= 0)
                            return "#cc4444"
                        if (modelData.indexOf("[WRN]") >= 0)
                            return "#cccc44"
                        if (modelData.indexOf("[INF]") >= 0)
                            return "#aaaaaa"
                        return "#666666"
                    }
                    font.family: "monospace"
                    font.pixelSize: 12
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                }

                // Auto-scroll to bottom
                onCountChanged: {
                    if (LogCapture.autoScroll)
                        Qt.callLater(function() { logView.positionViewAtEnd() })
                }
            }
        }
    }
}
