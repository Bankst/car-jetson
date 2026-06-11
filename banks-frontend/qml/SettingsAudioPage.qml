import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BanksFrontend

Item {
    id: root

    AudioController     { id: ac }
    AudioTestController { id: audioTest }

    property int selectedSlot: 0
    property var sel: ac.speakers[selectedSlot]

    function fmtHz(hz) { return hz >= 1000 ? (hz/1000).toFixed(hz % 1000 ? 2 : 0) + " kHz" : hz.toFixed(0) + " Hz" }

    Flickable {
        anchors.fill: parent
        anchors.margins: 20
        contentHeight: col.height
        clip: true

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 16

            // ---- HW status banner ----
            Rectangle {
                Layout.fillWidth: true
                visible: !ac.hardwareReady
                color: "#3a2a00"; radius: 6
                implicitHeight: bannerTxt.implicitHeight + 16
                Text {
                    id: bannerTxt
                    anchors.fill: parent; anchors.margins: 8
                    wrapMode: Text.Wrap; color: "#e0b050"; font.pixelSize: 12
                    text: "APE card / OPE1 PEQ not detected — controls are live in the UI but "
                        + "won't reach hardware until the AHUB route is up (banks-audio-route)."
                }
            }

            // ---- Master / global ----
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Text { text: "Master"; color: "#fff"; font.pixelSize: 20; font.weight: Font.DemiBold; Layout.preferredWidth: 90 }
                Slider {
                    id: masterVol
                    Layout.fillWidth: true
                    from: 0; to: 1.0
                    value: ac.masterVolume
                    onMoved: ac.setMasterVolume(value)
                }
                Text { text: ac.masterVolumeDb.toFixed(0) + " dB"; color: "#888"; font.pixelSize: 12; Layout.preferredWidth: 56 }
                Switch { text: "Mute"; checked: ac.masterMute; onToggled: ac.setMasterMute(checked) }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 20
                RowLayout {
                    spacing: 8
                    Text { text: "Mode"; color: "#aaa"; font.pixelSize: 14 }
                    ComboBox {
                        model: ac.speakerModes
                        currentIndex: ac.speakerModes.indexOf(ac.speakerMode)
                        onActivated: ac.setSpeakerMode(currentValue)
                        Layout.preferredWidth: 90
                    }
                }
                RowLayout {
                    spacing: 8
                    Text { text: "EQ Engine"; color: "#aaa"; font.pixelSize: 14 }
                    Switch { checked: ac.peqActive; onToggled: ac.setPeqActive(checked) }
                }
                Item { Layout.fillWidth: true }
                Text { text: "Bands: " + ac.bandMapName; color: "#666"; font.pixelSize: 11 }
                Text {
                    text: ac.codecPresent ? "CS42448 ✓" : "CS42448 —"
                    color: ac.codecPresent ? "#44cc44" : "#666"; font.pixelSize: 12
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // ---- Speakers: list + detail ----
            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                // Selector
                ColumnLayout {
                    Layout.preferredWidth: 190
                    Layout.alignment: Qt.AlignTop
                    spacing: 4
                    Repeater {
                        model: ac.speakers
                        delegate: ItemDelegate {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            height: 46
                            opacity: modelData.active ? 1.0 : 0.4
                            onClicked: root.selectedSlot = index
                            background: Rectangle {
                                radius: 5
                                color: root.selectedSlot === index ? "#2a2a30" : (index % 2 ? "#161618" : "#1b1b1f")
                            }
                            contentItem: RowLayout {
                                spacing: 6
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        leftPadding: 10; text: modelData.name
                                        color: root.selectedSlot === index ? "#fff" : "#aaa"
                                        font.pixelSize: 14; elide: Text.ElideRight
                                    }
                                    Text { leftPadding: 10; text: modelData.role; color: "#666"; font.pixelSize: 10 }
                                }
                                Text { visible: modelData.mute; text: "M"; color: "#cc4444"; font.pixelSize: 12; font.bold: true }
                                Text {
                                    rightPadding: 10
                                    text: (modelData.levelDb > 0 ? "+" : "") + modelData.levelDb.toFixed(0)
                                    color: "#777"; font.pixelSize: 12
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillHeight: true; width: 1; color: "#2a2a2a" }

                // Detail
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 12
                    enabled: root.sel !== undefined

                    // Name + role + active note
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        TextField {
                            Layout.preferredWidth: 170
                            text: root.sel ? root.sel.name : ""
                            readOnly: root.sel ? !root.sel.customizable : true
                            color: "#fff"; font.pixelSize: 16; font.weight: Font.DemiBold
                            background: Rectangle {
                                color: "transparent"
                                border.color: (root.sel && root.sel.customizable) ? "#444" : "transparent"
                                border.width: 1; radius: 4
                            }
                            onEditingFinished: if (root.sel && root.sel.customizable) ac.setName(root.selectedSlot, text)
                        }
                        Text { text: "Role"; color: "#aaa"; font.pixelSize: 13 }
                        ComboBox {
                            model: ac.roles
                            currentIndex: root.sel ? ac.roles.indexOf(root.sel.role) : 0
                            onActivated: ac.setRole(root.selectedSlot, currentValue)
                            Layout.preferredWidth: 150
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: root.sel && !root.sel.active
                            text: "inactive in " + ac.speakerMode; color: "#776633"; font.pixelSize: 11
                        }
                    }

                    // Level + mute + test
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: "Level"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 50 }
                        Slider {
                            id: lvl
                            Layout.fillWidth: true
                            from: -24; to: 6; stepSize: 0.5
                            value: root.sel ? root.sel.levelDb : 0
                            onMoved: ac.setLevel(root.selectedSlot, value)
                        }
                        Text { text: (lvl.value > 0 ? "+" : "") + lvl.value.toFixed(1) + " dB"; color: "#888"; font.pixelSize: 12; Layout.preferredWidth: 60 }
                        Switch { text: "Mute"; checked: root.sel ? root.sel.mute : false; onToggled: ac.setMute(root.selectedSlot, checked) }
                        Button { text: "Test"; flat: true; onClicked: ac.testSpeaker(root.selectedSlot) }
                    }

                    // ---- Crossover ----
                    Rectangle {
                        Layout.fillWidth: true
                        color: "#141418"; radius: 6
                        implicitHeight: xcol.implicitHeight + 20
                        ColumnLayout {
                            id: xcol
                            anchors.fill: parent; anchors.margins: 10
                            spacing: 8

                            RowLayout {
                                Text { text: "Crossover"; color: "#fff"; font.pixelSize: 14; font.weight: Font.DemiBold }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: "stages " + (root.sel ? root.sel.stagesUsed : 0) + "/" + (root.sel ? root.sel.stagesMax : 12)
                                    color: (root.sel && root.sel.eqBandsAvailable < ac.bandCount) ? "#e0b050" : "#666"
                                    font.pixelSize: 11
                                }
                            }

                            // High-pass
                            RowLayout {
                                spacing: 10
                                opacity: (root.sel && root.sel.ampLowcutOn) ? 0.4 : 1.0
                                enabled: !(root.sel && root.sel.ampLowcutOn)
                                Switch {
                                    text: "High-pass"
                                    checked: root.sel ? root.sel.hpOn : false
                                    onToggled: ac.setHp(root.selectedSlot, checked, root.sel.hpFreq, root.sel.hpStages)
                                }
                                Item { Layout.preferredWidth: 8 }
                                Text { text: "Freq"; color: "#aaa"; font.pixelSize: 12 }
                                TextField {
                                    Layout.preferredWidth: 80
                                    enabled: root.sel ? root.sel.hpOn : false
                                    text: root.sel ? root.sel.hpFreq.toFixed(0) : ""
                                    color: "#fff"; font.pixelSize: 13
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    validator: IntValidator { bottom: 20; top: 1000 }
                                    background: Rectangle { color: "#1f1f24"; radius: 4; border.color: "#333" }
                                    onEditingFinished: ac.setHp(root.selectedSlot, root.sel.hpOn, parseFloat(text), root.sel.hpStages)
                                }
                                Text { text: "Hz"; color: "#666"; font.pixelSize: 12 }
                                ComboBox {
                                    Layout.preferredWidth: 110
                                    enabled: root.sel ? root.sel.hpOn : false
                                    model: ["12 dB/oct", "24 dB/oct"]
                                    currentIndex: root.sel ? root.sel.hpStages - 1 : 1
                                    onActivated: ac.setHp(root.selectedSlot, root.sel.hpOn, root.sel.hpFreq, currentIndex + 1)
                                }
                            }

                            // Low-pass
                            RowLayout {
                                spacing: 10
                                Switch {
                                    text: "Low-pass "
                                    checked: root.sel ? root.sel.lpOn : false
                                    onToggled: ac.setLp(root.selectedSlot, checked, root.sel.lpFreq, root.sel.lpStages)
                                }
                                Item { Layout.preferredWidth: 8 }
                                Text { text: "Freq"; color: "#aaa"; font.pixelSize: 12 }
                                TextField {
                                    Layout.preferredWidth: 80
                                    enabled: root.sel ? root.sel.lpOn : false
                                    text: root.sel ? root.sel.lpFreq.toFixed(0) : ""
                                    color: "#fff"; font.pixelSize: 13
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    validator: IntValidator { bottom: 40; top: 20000 }
                                    background: Rectangle { color: "#1f1f24"; radius: 4; border.color: "#333" }
                                    onEditingFinished: ac.setLp(root.selectedSlot, root.sel.lpOn, parseFloat(text), root.sel.lpStages)
                                }
                                Text { text: "Hz"; color: "#666"; font.pixelSize: 12 }
                                ComboBox {
                                    Layout.preferredWidth: 110
                                    enabled: root.sel ? root.sel.lpOn : false
                                    model: ["12 dB/oct", "24 dB/oct"]
                                    currentIndex: root.sel ? root.sel.lpStages - 1 : 1
                                    onActivated: ac.setLp(root.selectedSlot, root.sel.lpOn, root.sel.lpFreq, currentIndex + 1)
                                }
                            }

                            // Amp owns the low-cut
                            RowLayout {
                                spacing: 10
                                Switch {
                                    text: "Amp low-cut"
                                    checked: root.sel ? root.sel.ampLowcutOn : false
                                    onToggled: ac.setAmpLowcut(root.selectedSlot, checked, root.sel.ampLowcutFreq)
                                }
                                Text {
                                    visible: root.sel ? root.sel.ampLowcutOn : false
                                    text: "@"; color: "#aaa"; font.pixelSize: 12
                                }
                                TextField {
                                    visible: root.sel ? root.sel.ampLowcutOn : false
                                    Layout.preferredWidth: 80
                                    text: root.sel ? root.sel.ampLowcutFreq.toFixed(0) : ""
                                    color: "#fff"; font.pixelSize: 13
                                    validator: IntValidator { bottom: 20; top: 1000 }
                                    background: Rectangle { color: "#1f1f24"; radius: 4; border.color: "#333" }
                                    onEditingFinished: ac.setAmpLowcut(root.selectedSlot, root.sel.ampLowcutOn, parseFloat(text))
                                }
                                Text {
                                    visible: root.sel ? root.sel.ampLowcutOn : false
                                    text: "Hz — OPE HP suppressed"; color: "#666"; font.pixelSize: 11
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }

                    // ---- Preset row ----
                    RowLayout {
                        spacing: 10
                        Text { text: "EQ"; color: "#aaa"; font.pixelSize: 14 }
                        Repeater {
                            model: ac.presets
                            delegate: Button {
                                required property var modelData
                                text: modelData; flat: true
                                onClicked: ac.applyPreset(root.selectedSlot, modelData)
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Button { text: "Reset"; flat: true; onClicked: ac.resetSpeaker(root.selectedSlot) }
                    }

                    // ---- EQ bands (count from active band map) ----
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 200
                        spacing: 2
                        Repeater {
                            model: ac.bandCount
                            delegate: ColumnLayout {
                                required property int index
                                Layout.fillWidth: true
                                spacing: 4
                                // grey out bands the channel's stage budget can't fit
                                property bool budgeted: root.sel && index < root.sel.eqBandsAvailable
                                opacity: budgeted ? 1.0 : 0.25
                                enabled: budgeted
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: {
                                        var g = root.sel ? root.sel.eqGains[index] : 0
                                        return (g > 0 ? "+" : "") + g.toFixed(1)
                                    }
                                    color: "#888"; font.pixelSize: 9
                                }
                                Slider {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.fillHeight: true
                                    orientation: Qt.Vertical
                                    from: -12; to: 12; stepSize: 0.5
                                    value: root.sel ? root.sel.eqGains[index] : 0
                                    onMoved: ac.setBand(root.selectedSlot, index, value)
                                }
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: ac.eqBandLabels[index]
                                    color: "#aaa"; font.pixelSize: 9
                                }
                            }
                        }
                    }
                }
            }

            Text { text: ac.statusText; color: "#666"; font.pixelSize: 12 }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // ---- Microphone (input test) ----
            Text { text: "Microphone"; color: "#fff"; font.pixelSize: 16; font.weight: Font.DemiBold }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Text { text: "Input"; color: "#aaa"; font.pixelSize: 14; Layout.preferredWidth: 50 }
                Rectangle {
                    Layout.fillWidth: true; height: 18; radius: 4; color: "#222"
                    Rectangle {
                        width: parent.width * audioTest.micLevel
                        height: parent.height; radius: 4
                        color: audioTest.micLevel > 0.8 ? "#cc4444" : audioTest.micLevel > 0.4 ? "#cccc44" : "#44cc44"
                        Behavior on width { NumberAnimation { duration: 30 } }
                    }
                }
                Button { text: "Rec 3s"; flat: true; onClicked: audioTest.recordMic(3) }
                Button { text: "Play"; flat: true; onClicked: audioTest.playRecording() }
            }
        }
    }
}
