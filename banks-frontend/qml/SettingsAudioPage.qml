import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BanksFrontend

Item {
    id: root

    AudioTestController { id: audioTest }

    Flickable {
        anchors.fill: parent
        anchors.margins: 24
        contentHeight: col.height
        clip: true

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 20

            // --- Speakers ---
            Text { text: "Speakers"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Text { text: "Master"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 100 }
                Slider {
                    id: masterVol
                    Layout.fillWidth: true
                    from: 0; to: 1.0; value: 0.5
                    onMoved: audioTest.setMasterVolume(value)
                }
                Text { text: Math.round(masterVol.value * 100) + "%"; color: "#888"; font.pixelSize: 12; Layout.preferredWidth: 40 }
                Button { text: "Test"; flat: true; onClicked: audioTest.testSpeakers() }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- AA Audio Channels ---
            Text { text: "AA Audio Channels"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            Repeater {
                model: [
                    { label: "Music (48k stereo)",     prop: "musicLevel",    node: "banks-aa-Music",        ch: 0 },
                    { label: "Navigation (16k mono)",  prop: "guidanceLevel", node: "banks-aa-Navigation",   ch: 1 },
                    { label: "System (16k mono)",      prop: "systemLevel",   node: "banks-aa-Notification", ch: 2 }
                ]
                delegate: ColumnLayout {
                    required property var modelData
                    spacing: 4
                    Layout.fillWidth: true

                    RowLayout {
                        spacing: 12
                        Text { text: modelData.label; color: "#aaa"; font.pixelSize: 13; Layout.preferredWidth: 180 }
                        Rectangle {
                            Layout.fillWidth: true; height: 10; radius: 5; color: "#222"
                            Rectangle {
                                property real level: {
                                    if (modelData.prop === "musicLevel") return aaSession.musicLevel
                                    if (modelData.prop === "guidanceLevel") return aaSession.guidanceLevel
                                    return aaSession.systemLevel
                                }
                                width: parent.width * Math.min(1.0, level)
                                height: parent.height; radius: 5
                                color: level > 0.8 ? "#cc4444" : level > 0.3 ? "#44cc44" : "#336633"
                                Behavior on width { NumberAnimation { duration: 30 } }
                            }
                        }
                        Button { text: "Test"; flat: true; onClicked: audioTest.testChannel(modelData.ch) }
                    }
                }
            }

            Text { text: "Levels update when phone is streaming"; color: "#555"; font.pixelSize: 11 }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Microphone ---
            Text { text: "Microphone"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Text { text: "Input Level"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 100 }
                Rectangle {
                    Layout.fillWidth: true; height: 20; radius: 4; color: "#222"
                    Rectangle {
                        width: parent.width * audioTest.micLevel
                        height: parent.height; radius: 4
                        color: audioTest.micLevel > 0.8 ? "#cc4444"
                             : audioTest.micLevel > 0.4 ? "#cccc44"
                             : "#44cc44"
                        Behavior on width { NumberAnimation { duration: 30 } }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: Math.round(audioTest.micLevel * 100) + "%"
                        color: "#ccc"; font.pixelSize: 11
                    }
                }
                Text {
                    text: audioTest.micLevel > 0.8 ? "CLIP" :
                          audioTest.micLevel > 0.01 ? "OK" : "—"
                    color: audioTest.micLevel > 0.8 ? "#cc4444" :
                           audioTest.micLevel > 0.01 ? "#44cc44" : "#555"
                    font.pixelSize: 12; Layout.preferredWidth: 35
                }
            }

            RowLayout {
                spacing: 12
                Text { text: "Gain"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 100 }
                Slider {
                    id: micGain
                    Layout.fillWidth: true
                    from: 0; to: 1.5; value: 0.5
                    onMoved: audioTest.setMicGain(value)
                }
                Text { text: Math.round(micGain.value * 100) + "%"; color: "#888"; font.pixelSize: 12; Layout.preferredWidth: 40 }
            }

            RowLayout {
                spacing: 12
                Button { text: "Record 3s"; flat: true; onClicked: audioTest.recordMic(3) }
                Button { text: "Play Recording"; flat: true; onClicked: audioTest.playRecording() }
            }

            Text { text: audioTest.statusText; color: "#666"; font.pixelSize: 12 }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- Display ---
            Text { text: "Display"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold }

            RowLayout {
                spacing: 12
                Text { text: "Spectrum Analyzer"; color: "#aaa"; font.pixelSize: 14 }
                Switch {
                    id: specToggle
                    checked: Window.window ? Window.window.spectrumVisible : true
                    onToggled: { if (Window.window) Window.window.spectrumVisible = checked }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // --- PipeWire ---
            Text { text: "PipeWire"; color: "#fff"; font.pixelSize: 16; font.weight: Font.DemiBold }
            Text { text: audioTest.pipewireInfo; color: "#666"; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }

    property var aaSession: Window.window && Window.window.aaSession ? Window.window.aaSession : dummySession

    QtObject {
        id: dummySession
        property real musicLevel: 0
        property real guidanceLevel: 0
        property real systemLevel: 0
    }
}
