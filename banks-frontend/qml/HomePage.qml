import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    signal openVisualizer()  // wired by Main.qml to switch views.currentIndex

    Rectangle {
        anchors.fill: parent
        anchors.bottomMargin: 48
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#101820" }
            GradientStop { position: 1.0; color: "#000000" }
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 24

            Text {
                text: "Banks"
                color: "#e0e0e0"
                font.pixelSize: 64
                Layout.alignment: Qt.AlignHCenter
            }

            Button {
                text: "Open Visualizer"
                Layout.alignment: Qt.AlignHCenter
                onClicked: root.openVisualizer()
            }
        }
    }
}
