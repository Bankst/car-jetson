import QtQuick

Item {
    id: overlay
    anchors.fill: parent
    visible: false
    z: 99999

    property bool active: visible
    readonly property var palette: ["#FF4444","#44FF44","#4488FF","#FFAA00","#FF44FF","#44FFFF","#FFFF44","#FF8844","#88FF44","#AA44FF"]

    function show() { visible = true; canvas.clear() }
    function hide() { visible = false }

    Rectangle {
        anchors.fill: parent
        color: "#E0000000"
    }

    Repeater {
        model: 5
        Rectangle {
            y: (index + 1) * overlay.height / 6
            width: overlay.width; height: 1
            color: "#30ffffff"
        }
    }
    Repeater {
        model: 9
        Rectangle {
            x: (index + 1) * overlay.width / 10
            width: 1; height: overlay.height
            color: "#30ffffff"
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        property var trails: ({})

        function clear() {
            trails = {}
            var ctx = getContext("2d")
            if (ctx) { ctx.clearRect(0, 0, width, height) }
            requestPaint()
        }

        function addPoint(tid, x, y) {
            if (!trails[tid]) trails[tid] = []
            trails[tid].push(Qt.point(x, y))
            requestPaint()
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            for (var tid in trails) {
                var pts = trails[tid]
                if (pts.length < 2) continue
                var ci = parseInt(tid) % overlay.palette.length
                ctx.strokeStyle = overlay.palette[ci]
                ctx.globalAlpha = 0.3
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.moveTo(pts[0].x, pts[0].y)
                for (var i = 1; i < pts.length; i++)
                    ctx.lineTo(pts[i].x, pts[i].y)
                ctx.stroke()
            }
            ctx.globalAlpha = 1.0
        }
    }

    ListModel { id: touchPoints }

    Repeater {
        model: touchPoints
        Rectangle {
            x: model.px - 20; y: model.py - 20
            width: 40; height: 40; radius: 20
            color: overlay.palette[model.ci % overlay.palette.length]
            opacity: model.alive ? 0.9 : 0.25

            Text {
                anchors.centerIn: parent
                text: model.tid
                color: "white"
                font.pixelSize: 12; font.bold: true
            }

            Text {
                anchors { top: parent.bottom; topMargin: 2; horizontalCenter: parent.horizontalCenter }
                text: model.px.toFixed(0) + "," + model.py.toFixed(0)
                color: overlay.palette[model.ci % overlay.palette.length]
                font.pixelSize: 10
                visible: model.alive
            }
        }
    }

    Text {
        id: infoText
        anchors { top: parent.top; left: parent.left; margins: 16 }
        color: "white"; font.pixelSize: 18
        property string lastCoords: ""
        text: "Touch Debug — " + overlay.width.toFixed(0) + "x" + overlay.height.toFixed(0) + "\n" + lastCoords
    }

    Rectangle {
        id: backBtn
        anchors.centerIn: parent
        width: 120; height: 60; radius: 8
        color: backMa.pressed ? "#FF4444" : "#333"
        border.color: "#888"; border.width: 1
        z: 1

        Text {
            anchors.centerIn: parent
            color: "white"; font.pixelSize: 14
            text: backMa.pressed ? "Hold..." : "BACK\n(long hold)"
            horizontalAlignment: Text.AlignHCenter
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.margins: 2
            height: 4; radius: 2
            color: "#FF4444"
            width: (parent.width - 4) * holdTimer.progress
        }

        MouseArea {
            id: backMa
            anchors.fill: parent
            z: 1
            onPressed: holdTimer.start()
            onReleased: holdTimer.stop()
            onCanceled: holdTimer.stop()
        }

        Timer {
            id: holdTimer
            interval: 50; repeat: true
            property real progress: 0
            onTriggered: {
                progress += 0.05
                if (progress >= 1.0) { stop(); progress = 0; overlay.hide() }
            }
            onRunningChanged: if (!running) progress = 0
        }
    }

    MultiPointTouchArea {
        anchors.fill: parent
        minimumTouchPoints: 1
        maximumTouchPoints: 10
        mouseEnabled: true

        property int nextColorIdx: 0
        property var colorMap: ({})

        function getColor(tid) {
            if (colorMap[tid] === undefined) {
                colorMap[tid] = nextColorIdx++
            }
            return colorMap[tid]
        }

        onPressed: function(points) {
            for (var i = 0; i < points.length; i++) {
                var ci = getColor(points[i].pointId)
                touchPoints.append({ tid: points[i].pointId, px: points[i].x, py: points[i].y, alive: true, ci: ci })
                canvas.addPoint(points[i].pointId, points[i].x, points[i].y)
                infoText.lastCoords = "PRESS #" + points[i].pointId + ": " + points[i].x.toFixed(1) + ", " + points[i].y.toFixed(1)
            }
        }

        onUpdated: function(points) {
            for (var i = 0; i < points.length; i++) {
                canvas.addPoint(points[i].pointId, points[i].x, points[i].y)
                infoText.lastCoords = "MOVE #" + points[i].pointId + ": " + points[i].x.toFixed(1) + ", " + points[i].y.toFixed(1)
                for (var j = 0; j < touchPoints.count; j++) {
                    if (touchPoints.get(j).tid === points[i].pointId) {
                        touchPoints.set(j, { tid: points[i].pointId, px: points[i].x, py: points[i].y, alive: true, ci: touchPoints.get(j).ci })
                        break
                    }
                }
            }
        }

        onReleased: function(points) {
            for (var i = 0; i < points.length; i++) {
                for (var j = 0; j < touchPoints.count; j++) {
                    if (touchPoints.get(j).tid === points[i].pointId) {
                        touchPoints.set(j, { tid: points[i].pointId, px: points[i].x, py: points[i].y, alive: false, ci: touchPoints.get(j).ci })
                        break
                    }
                }
            }
        }
    }
}
