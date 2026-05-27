import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property var appWindow: Window.window

    Flickable {
        anchors.fill: parent
        anchors.margins: 24
        contentHeight: col.height
        clip: true

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 20

            // --- Window Mode ---
            Text { text: "Window Mode"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Text { text: "Fullscreen"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 120 }
                Switch {
                    id: fullscreenToggle
                    checked: appWindow ? appWindow.visibility === Window.FullScreen : false
                    onToggled: {
                        if (appWindow) {
                            appWindow.visibility = checked ? Window.FullScreen : Window.Windowed
                        }
                    }
                }
            }

            GridLayout {
                columns: 2; columnSpacing: 24; rowSpacing: 12; Layout.fillWidth: true

                Text { text: "Window Size"; color: "#aaa"; font.pixelSize: 14 }
                Text {
                    text: appWindow ? appWindow.width + " x " + appWindow.height : "—"
                    color: "#888"; font.pixelSize: 14
                }

                Text { text: "Screen (logical)"; color: "#aaa"; font.pixelSize: 14 }
                Text {
                    text: Screen.width + " x " + Screen.height
                    color: "#888"; font.pixelSize: 14
                }

                Text { text: "Qt DPR"; color: "#aaa"; font.pixelSize: 14 }
                Text {
                    text: Screen.devicePixelRatio.toFixed(2) + "x"
                    color: "#888"; font.pixelSize: 14
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Window Size Presets ---
            Text { text: "Window Size Presets"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            Text {
                text: "Disabled while fullscreen"
                color: "#555"; font.pixelSize: 11
                visible: fullscreenToggle.checked
            }

            ButtonGroup { id: sizeGroup }

            Repeater {
                model: [
                    { label: "1920 x 1080", w: 1920, h: 1080 },
                    { label: "1280 x 720",  w: 1280, h: 720  },
                    { label: "960 x 540",   w: 960,  h: 540  }
                ]
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    spacing: 12
                    Layout.fillWidth: true

                    RadioButton {
                        id: radio
                        ButtonGroup.group: sizeGroup
                        enabled: !fullscreenToggle.checked
                        checked: appWindow ? (appWindow.width === modelData.w && appWindow.height === modelData.h) : false
                        onClicked: {
                            if (appWindow && !fullscreenToggle.checked) {
                                appWindow.width = modelData.w
                                appWindow.height = modelData.h
                            }
                        }

                        indicator: Rectangle {
                            implicitWidth: 18; implicitHeight: 18
                            radius: 9; border.color: radio.enabled ? "#aaa" : "#555"; border.width: 1
                            color: "transparent"
                            Rectangle {
                                anchors.centerIn: parent
                                width: 10; height: 10; radius: 5
                                color: radio.checked ? "#44cc44" : "transparent"
                            }
                        }
                    }
                    Text {
                        text: modelData.label
                        color: fullscreenToggle.checked ? "#555" : "#aaa"
                        font.pixelSize: 14
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Android Auto Video ---
            Text { text: "Android Auto Video"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Text { text: "Auto-start"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 120 }
                Switch {
                    id: aaAutoStartToggle
                    checked: {
                        var s = Window.window ? Window.window.aaSession : null
                        return s ? s.autoStart : true
                    }
                    onToggled: {
                        var s = Window.window ? Window.window.aaSession : null
                        if (s) s.autoStart = checked
                    }
                }
                Text { text: "Start AA session on app launch"; color: "#555"; font.pixelSize: 11 }
            }

            Text {
                text: "Resolution/FPS changes take effect on next session"
                color: "#555"; font.pixelSize: 11
            }

            GridLayout {
                columns: 2; columnSpacing: 24; rowSpacing: 12; Layout.fillWidth: true

                Text { text: "Resolution"; color: "#aaa"; font.pixelSize: 14 }
                ComboBox {
                    id: aaResCombo
                    Layout.preferredWidth: 200
                    model: ListModel {
                        ListElement { text: "800 x 480";   value: 1 }
                        ListElement { text: "1280 x 720";  value: 2 }
                        ListElement { text: "1920 x 1080"; value: 3 }
                    }
                    textRole: "text"
                    currentIndex: {
                        var s = Window.window ? Window.window.aaSession : null
                        if (!s) return 2
                        var v = s.videoResolution
                        if (v === 1) return 0
                        if (v === 2) return 1
                        return 2
                    }
                    onActivated: function(index) {
                        var s = Window.window ? Window.window.aaSession : null
                        if (s) s.videoResolution = model.get(index).value
                    }

                    background: Rectangle { color: "#2a2a30"; radius: 6; border.color: "#444"; border.width: 1 }
                    contentItem: Text {
                        text: aaResCombo.displayText; color: "#ccc"; font.pixelSize: 14
                        leftPadding: 8; verticalAlignment: Text.AlignVCenter
                    }
                    popup: Popup {
                        y: aaResCombo.height
                        width: aaResCombo.width
                        padding: 1
                        contentItem: ListView {
                            implicitHeight: contentHeight
                            model: aaResCombo.delegateModel
                            clip: true
                        }
                        background: Rectangle { color: "#2a2a30"; border.color: "#555"; radius: 4 }
                    }
                    delegate: ItemDelegate {
                        width: aaResCombo.width
                        contentItem: Text { text: model.text; color: "#ccc"; font.pixelSize: 14; leftPadding: 8 }
                        background: Rectangle { color: highlighted ? "#3a3a40" : "transparent" }
                        highlighted: aaResCombo.highlightedIndex === index
                    }
                }

                Text { text: "Frame Rate"; color: "#aaa"; font.pixelSize: 14 }
                ComboBox {
                    id: aaFpsCombo
                    Layout.preferredWidth: 200
                    model: ListModel {
                        ListElement { text: "60 fps"; value: 1 }
                        ListElement { text: "30 fps"; value: 2 }
                    }
                    textRole: "text"
                    currentIndex: {
                        var s = Window.window ? Window.window.aaSession : null
                        if (!s) return 0
                        return s.videoFps === 2 ? 1 : 0
                    }
                    onActivated: function(index) {
                        var s = Window.window ? Window.window.aaSession : null
                        if (s) s.videoFps = model.get(index).value
                    }

                    background: Rectangle { color: "#2a2a30"; radius: 6; border.color: "#444"; border.width: 1 }
                    contentItem: Text {
                        text: aaFpsCombo.displayText; color: "#ccc"; font.pixelSize: 14
                        leftPadding: 8; verticalAlignment: Text.AlignVCenter
                    }
                    popup: Popup {
                        y: aaFpsCombo.height
                        width: aaFpsCombo.width
                        padding: 1
                        contentItem: ListView {
                            implicitHeight: contentHeight
                            model: aaFpsCombo.delegateModel
                            clip: true
                        }
                        background: Rectangle { color: "#2a2a30"; border.color: "#555"; radius: 4 }
                    }
                    delegate: ItemDelegate {
                        width: aaFpsCombo.width
                        contentItem: Text { text: model.text; color: "#ccc"; font.pixelSize: 14; leftPadding: 8 }
                        background: Rectangle { color: highlighted ? "#3a3a40" : "transparent" }
                        highlighted: aaFpsCombo.highlightedIndex === index
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Rendering ---
            Text { text: "Rendering"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Text { text: "VSync"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 120 }
                Switch {
                    id: vsyncToggle
                    checked: true
                    enabled: false
                }
                Text { text: "(controlled by Qt runtime)"; color: "#555"; font.pixelSize: 11 }
            }

            RowLayout {
                spacing: 12
                Text { text: "Frame Rate"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 120 }
                Text {
                    id: fpsText
                    property int fps: 0
                    text: fps > 0 ? fps + " fps" : "—"
                    color: "#888"; font.pixelSize: 14

                    property real lastTime: 0
                    property int frameCount: 0

                    NumberAnimation on opacity {
                        id: fpsRefresh
                        from: 1; to: 1; duration: 1000
                        loops: Animation.Infinite
                        running: root.visible
                    }

                    Timer {
                        interval: 1000
                        repeat: true
                        running: root.visible
                        onTriggered: {
                            fpsText.fps = fpsText.frameCount
                            fpsText.frameCount = 0
                        }
                    }

                    FrameAnimation {
                        running: root.visible
                        onTriggered: fpsText.frameCount++
                    }
                }
            }
        }
    }
}
