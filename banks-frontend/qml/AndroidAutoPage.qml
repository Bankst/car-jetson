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

    function onTabActivated()  { aaSession.activate()   }
    function onTabDeactivated(){ aaSession.deactivate()  }
}
