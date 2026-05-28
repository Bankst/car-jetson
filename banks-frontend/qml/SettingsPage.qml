import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 160
            color: "#141418"

            ListView {
                id: sideNav
                anchors.fill: parent
                anchors.topMargin: 12
                currentIndex: 0
                model: ListModel {
                    ListElement { label: "Audio";   page: 0 }
                    ListElement { label: "Display"; page: 1 }
                    ListElement { label: "Viz";     page: 2 }
                    ListElement { label: "CAN";     page: 3 }
                    ListElement { label: "Media";   page: 4 }
                    ListElement { label: "System";  page: 5 }
                    ListElement { label: "Logs";    page: 6 }
                }
                delegate: ItemDelegate {
                    width: sideNav.width
                    height: 40
                    highlighted: sideNav.currentIndex === index
                    onClicked: sideNav.currentIndex = index

                    contentItem: Text {
                        text: model.label
                        color: sideNav.currentIndex === index ? "#ffffff" : "#888888"
                        font.pixelSize: 14
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 16
                    }
                    background: Rectangle {
                        color: sideNav.currentIndex === index ? "#2a2a30" : "transparent"
                    }
                }
            }
        }

        // Divider
        Rectangle { Layout.fillHeight: true; width: 1; color: "#333" }

        // Content
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: sideNav.currentIndex

            SettingsAudioPage {}
            SettingsDisplayPage {}
            SettingsVizPage {}
            SettingsCanPage {}
            SettingsMediaPage {}
            SettingsSystemPage {}
            SettingsLogsPage {}
        }
    }
}
