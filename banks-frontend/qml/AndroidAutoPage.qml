import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BanksFrontend

Item {
    id: root
    property alias aaSession: aaSession

    AASessionController {
        id: aaSession
    }

    // -- Full-screen video (visible once phone streams) --
    AAVideoItem {
        id: aaVideo
        anchors.fill: parent
        visible: aaSession.sessionActive
        session: aaSession
    }

    // -- Waiting / disconnected overlay (no video yet) --
    Rectangle {
        id: waitingOverlay
        anchors.fill: parent
        color: "#1a1a1a"
        visible: !aaSession.connected

        Column {
            anchors.centerIn: parent
            spacing: 16

            // Connection icon — pulsing dot
            Rectangle {
                id: connDot
                width: 12; height: 12; radius: 6
                color: aaSession.status.startsWith("Disconnected") ? "#cc4444"
                     : aaSession.status.startsWith("Connection stale") ? "#cc8833"
                     : "#4488cc"
                anchors.horizontalCenter: parent.horizontalCenter

                SequentialAnimation on opacity {
                    running: !aaSession.connected
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 800; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
                }
            }

            Text {
                text: "Android Auto"
                color: "#ffffff"
                font.pixelSize: 32
                font.weight: Font.DemiBold
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: aaSession.status
                color: aaSession.status.startsWith("Disconnected") ? "#cc6666"
                     : aaSession.status.startsWith("Connection stale") ? "#cc9955"
                     : "#888888"
                font.pixelSize: 16
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Rectangle {
                visible: !aaSession.connected
                anchors.horizontalCenter: parent.horizontalCenter
                width: wirelessLabel.width + 32
                height: wirelessLabel.height + 16
                radius: 8
                color: wirelessMa.pressed ? "#334433" : (aaSession.wirelessActive ? "#283828" : "#282830")
                border.color: aaSession.wirelessActive ? "#44cc44" : "#444"
                border.width: 1

                Text {
                    id: wirelessLabel
                    anchors.centerIn: parent
                    text: aaSession.wirelessActive ? "Wireless AA Active" : "Start Wireless AA"
                    color: aaSession.wirelessActive ? "#66cc66" : "#aaaaaa"
                    font.pixelSize: 14
                }

                MouseArea {
                    id: wirelessMa
                    anchors.fill: parent
                    onClicked: {
                        if (aaSession.wirelessActive)
                            aaSession.stopWireless()
                        else
                            aaSession.startWireless()
                    }
                }
            }
        }
    }

    // -- Status pill overlay (visible even when video is playing) --
    Rectangle {
        id: statusPill
        visible: aaSession.connected && aaSession.sessionActive
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 12
        width: pillRow.width + 16
        height: pillRow.height + 8
        radius: height / 2
        color: "#44000000"
        opacity: 0.0

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                width: 8; height: 8; radius: 4
                color: aaSession.status.startsWith("Android Auto active") ? "#44cc44"
                     : aaSession.status.startsWith("Connection stale") ? "#cc8833"
                     : "#cc4444"
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: aaSession.status
                color: "#dddddd"
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Fade in when status changes, auto-hide after 3s
        Behavior on opacity { NumberAnimation { duration: 300 } }

        Timer {
            id: statusPillFade
            interval: 3000
            onTriggered: statusPill.opacity = 0.0
        }

        Connections {
            target: aaSession
            function onStatusChanged() {
                statusPill.opacity = 1.0
                statusPillFade.restart()
            }
        }
    }

    // -- Mic VU meter (bottom-right, visible when mic active) --
    Rectangle {
        id: vuMeter
        visible: aaSession.micLevel > 0.001
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        width: 8
        height: 80
        radius: 4
        color: "#30ffffff"

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            height: parent.height * Math.min(1.0, aaSession.micLevel)
            radius: 4
            color: aaSession.micLevel > 0.8 ? "#cc4444"
                 : aaSession.micLevel > 0.4 ? "#cccc44"
                 : "#44cc44"

            Behavior on height { NumberAnimation { duration: 30 } }
        }
    }

    // -- Bluetooth pairing confirmation dialog --
    Rectangle {
        id: pairingOverlay
        anchors.fill: parent
        color: "#a0000000"
        visible: aaSession.pairingAgent && aaSession.pairingAgent.pairingPending
        z: 9999

        // Block mouse events from reaching controls underneath
        MouseArea { anchors.fill: parent }

        Rectangle {
            id: pairingDialog
            anchors.centerIn: parent
            width: 440
            height: dialogColumn.height + 48
            radius: 16
            color: "#1e1e24"
            border.color: "#303040"
            border.width: 1

            Column {
                id: dialogColumn
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 24
                width: parent.width - 48
                spacing: 16

                Text {
                    text: "Bluetooth Pairing"
                    color: "#ffffff"
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Text {
                    text: aaSession.pairingAgent
                          ? "Pair with " + aaSession.pairingAgent.deviceName + "?"
                          : ""
                    color: "#bbbbbb"
                    font.pixelSize: 16
                    wrapMode: Text.WordWrap
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    text: "Confirm the PIN matches on both devices:"
                    color: "#888888"
                    font.pixelSize: 13
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                // Large PIN display
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: pinText.width + 48
                    height: pinText.height + 20
                    radius: 8
                    color: "#282830"

                    Text {
                        id: pinText
                        anchors.centerIn: parent
                        text: aaSession.pairingAgent ? aaSession.pairingAgent.passkey : ""
                        color: "#55aaff"
                        font.pixelSize: 42
                        font.weight: Font.Bold
                        font.family: "monospace"
                        font.letterSpacing: 8
                    }
                }

                // Timeout progress bar
                Item {
                    width: parent.width
                    height: 4

                    Rectangle {
                        width: parent.width
                        height: parent.height
                        radius: 2
                        color: "#202028"
                    }

                    Rectangle {
                        id: timeoutBar
                        height: parent.height
                        radius: 2
                        color: "#55aaff"
                        width: parent.width

                        NumberAnimation on width {
                            id: timeoutAnim
                            to: 0
                            duration: 30000
                            running: false
                        }
                    }
                }

                // Buttons
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 16

                    Rectangle {
                        width: 140; height: 44
                        radius: 8
                        color: rejectMa.pressed ? "#553333" : "#402828"

                        Text {
                            anchors.centerIn: parent
                            text: "Reject"
                            color: "#cc6666"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            id: rejectMa
                            anchors.fill: parent
                            onClicked: {
                                if (aaSession.pairingAgent)
                                    aaSession.pairingAgent.confirmPairing(false)
                            }
                        }
                    }

                    Rectangle {
                        width: 140; height: 44
                        radius: 8
                        color: acceptMa.pressed ? "#335533" : "#284028"

                        Text {
                            anchors.centerIn: parent
                            text: "Confirm"
                            color: "#66cc66"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            id: acceptMa
                            anchors.fill: parent
                            onClicked: {
                                if (aaSession.pairingAgent)
                                    aaSession.pairingAgent.confirmPairing(true)
                            }
                        }
                    }
                }
            }
        }

        // Auto-reject on timeout
        Timer {
            id: pairingTimeout
            interval: 30000
            running: false
            onTriggered: {
                if (aaSession.pairingAgent && aaSession.pairingAgent.pairingPending)
                    aaSession.pairingAgent.confirmPairing(false)
            }
        }

        // Start/stop timeout when dialog shows/hides
        onVisibleChanged: {
            if (visible) {
                timeoutBar.width = Qt.binding(function() { return timeoutBar.parent.width })
                timeoutAnim.restart()
                pairingTimeout.restart()
            } else {
                timeoutAnim.stop()
                pairingTimeout.stop()
            }
        }
    }

    function onTabActivated()  { aaSession.activate()   }
    function onTabDeactivated(){ aaSession.deactivate()  }
}
