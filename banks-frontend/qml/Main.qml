import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BanksFrontend

ApplicationWindow {
    id: root
    visible: true
    visibility: Window.Windowed

    readonly property int targetW: 1920
    readonly property int targetH: 1080
    width:  1920
    height: 1080
    minimumWidth: 960; minimumHeight: 540
    title: "Banks"

    color: "#000"

    // Persistent views — all kept alive in StackLayout. No destroy, no anim, no reset.
    StackLayout {
        id: views
        anchors {
            left:   parent.left
            right:  parent.right
            top:    parent.top
            bottom: nav.top
        }
        currentIndex: 0

        HomePage       { onOpenVisualizer: views.currentIndex = 1 }
        VisualizerPage { /* persistent — projectM lives as long as window */ }
        Item { /* media placeholder */ }
        Item { /* CAN placeholder */ }
    }

    // Footer nav — current view highlighted
    Rectangle {
        id: nav
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 48
        color: "#101015"

        RowLayout {
            anchors.fill: parent
            spacing: 0
            Repeater {
                model: [
                    { label: "Home",  idx: 0, enabled: true  },
                    { label: "Viz",   idx: 1, enabled: true  },
                    { label: "Media", idx: 2, enabled: false },
                    { label: "CAN",   idx: 3, enabled: false }
                ]
                delegate: Button {
                    required property var modelData
                    Layout.fillWidth: true
                    flat: true
                    text: modelData.label
                    enabled: modelData.enabled
                    highlighted: views.currentIndex === modelData.idx
                    onClicked: views.currentIndex = modelData.idx
                }
            }
        }
    }

    // Persistent spectrum-analyzer overlay — draggable + resizable.
    Item {
        id: spectrumFrame
        x: 16
        y: 16
        width:  420
        height: 120
        z: 1000

        // Body — semi-transparent dark backdrop
        Rectangle {
            anchors.fill: parent
            color: "#80000000"
            border.color: "#40ffffff"
            border.width: 1
            radius: 4
        }

        SpectrumWidget {
            id: spectrum
            anchors.fill: parent
            anchors.margins: 4
            bandCount: 48
        }

        // Drag header strip — top 12px
        MouseArea {
            id: dragArea
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top:   parent.top
            height: 12
            cursorShape: Qt.SizeAllCursor
            drag.target: spectrumFrame
            drag.axis: Drag.XAndYAxis
            drag.minimumX: 0
            drag.minimumY: 0
            drag.maximumX: root.width  - spectrumFrame.width
            drag.maximumY: root.height - spectrumFrame.height
        }

        // Resize grip — driven by a transparent full-frame MouseArea that
        // only activates when pressed in the bottom-right corner. Keeping the
        // grip out of the anchor system avoids re-layout feedback during drag.
        Rectangle {
            id: gripVis
            width: 14; height: 14
            x: spectrumFrame.width  - width
            y: spectrumFrame.height - height
            color: gripMa.pressed ? "#a0ffffff" : "#40ffffff"
            radius: 2
        }

        MouseArea {
            id: gripMa
            // 24px hit box in bottom-right corner
            x: spectrumFrame.width  - 24
            y: spectrumFrame.height - 24
            width: 24; height: 24
            cursorShape: Qt.SizeFDiagCursor

            property real pressWinX: 0
            property real pressWinY: 0
            property real startW: 0
            property real startH: 0
            property int  moveCount: 0

            onPressed: function(mouse) {
                var p = mapToItem(null, mouse.x, mouse.y)
                pressWinX = p.x; pressWinY = p.y
                startW    = spectrumFrame.width
                startH    = spectrumFrame.height
                moveCount = 0
                console.log("[spec resize] press win=", p.x.toFixed(1), p.y.toFixed(1),
                            " start=", startW, "x", startH)
            }
            onReleased: console.log("[spec resize] release after", moveCount, "moves")
            onPositionChanged: function(mouse) {
                if (!pressed) return
                var p = mapToItem(null, mouse.x, mouse.y)
                var nw = Math.max(160, startW + (p.x - pressWinX))
                var nh = Math.max(60,  startH + (p.y - pressWinY))
                spectrumFrame.width  = nw
                spectrumFrame.height = nh
                if ((++moveCount % 5) === 0) {
                    console.log("[spec resize] move win=", p.x.toFixed(1), p.y.toFixed(1),
                                " size=", nw.toFixed(1), "x", nh.toFixed(1))
                }
            }
        }
    }
}
