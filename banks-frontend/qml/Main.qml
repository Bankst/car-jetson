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
    property bool spectrumVisible: true
    property alias aaSession: aaPage.aaSession
    property alias visualizer: vizPage.visualizer
    property alias spectrumWidget: spectrum
    minimumWidth: 640; minimumHeight: 480

    Component.onCompleted: {
        if (visibility === Window.Windowed) {
            width  = Math.min(targetW, Math.round(Screen.desktopAvailableWidth * 0.8))
            height = Math.min(targetH, Math.round(Screen.desktopAvailableHeight * 0.8))
        }
    }
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
        onCurrentIndexChanged: {
            if (currentIndex === 2) aaPage.onTabActivated()
        }

        HomePage       { onOpenVisualizer: views.currentIndex = 1 }
        VisualizerPage { id: vizPage; Component.onCompleted: visualizer.active = Qt.binding(function() { return views.currentIndex === 1 && root.active }) }
        AndroidAutoPage { id: aaPage }
        Item { /* CAN placeholder */ }
        SettingsPage {}
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
                    { label: "AA",    idx: 2, enabled: true  },
                    { label: "CAN",   idx: 3, enabled: false },
                    { label: "Settings", idx: 4, enabled: true  }
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
    // Snaps to top-right when AA tab active (maps area), top-left otherwise.
    Item {
        id: spectrumFrame
        visible: root.spectrumVisible && views.currentIndex !== 4
        x: views.currentIndex === 2 ? root.width - width - 16 : 16
        y: 16
        width:  420
        height: 120
        z: 1000

        Behavior on x { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

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

        // Drag strip — bottom 12px
        MouseArea {
            id: dragArea
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
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
            x: 0
            y: spectrumFrame.height - height
            color: gripMa.pressed ? "#a0ffffff" : "#40ffffff"
            radius: 2
        }

        MouseArea {
            id: gripMa
            // 24px hit box in bottom-left corner
            x: 0
            y: spectrumFrame.height - 24
            width: 24; height: 24
            cursorShape: Qt.SizeBDiagCursor

            property real pressWinX: 0
            property real pressWinY: 0
            property real startW: 0
            property real startH: 0
            property real startX: 0
            property int  moveCount: 0

            onPressed: function(mouse) {
                var p = mapToItem(null, mouse.x, mouse.y)
                pressWinX = p.x; pressWinY = p.y
                startW    = spectrumFrame.width
                startH    = spectrumFrame.height
                startX    = spectrumFrame.x
                moveCount = 0
            }
            onReleased: function() {}
            onPositionChanged: function(mouse) {
                if (!pressed) return
                var p = mapToItem(null, mouse.x, mouse.y)
                var dx = p.x - pressWinX
                var nw = Math.max(160, startW - dx)
                var nh = Math.max(60,  startH + (p.y - pressWinY))
                spectrumFrame.x      = startX + (startW - nw)
                spectrumFrame.width   = nw
                spectrumFrame.height  = nh
                if ((++moveCount % 5) === 0) {
                    console.log("[spec resize] move win=", p.x.toFixed(1), p.y.toFixed(1),
                                " size=", nw.toFixed(1), "x", nh.toFixed(1))
                }
            }
        }
    }
}
