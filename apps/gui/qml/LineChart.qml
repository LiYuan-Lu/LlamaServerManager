import QtQuick

// Lightweight time-series chart drawn on a Canvas (no Qt Charts dependency).
// `times` (ms since epoch) and `values` are parallel arrays, oldest first. Negative or
// missing values are drawn as gaps, as are pauses longer than `gapMs` (e.g. Agent offline).
Canvas {
    id: chart
    property var times: []
    property var values: []
    property real maxValue: 100
    property color color: "#38bdf8"
    property int windowMs: 300000
    property int gapMs: 10000
    property string emptyText: ""

    onTimesChanged: requestPaint()
    onValuesChanged: requestPaint()
    onColorChanged: requestPaint()
    onMaxValueChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function point(i, start) {
        const x = (chart.times[i] - start) / chart.windowMs * width
        const v = Math.min(Math.max(chart.values[i] / chart.maxValue, 0), 1)
        return { x: x, y: height - 1 - v * (height - 2) }
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.08)
        ctx.lineWidth = 1
        for (const f of [0.25, 0.5, 0.75]) {
            const y = Math.round(height * f) + 0.5
            ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
        }
        const n = Math.min(chart.times.length, chart.values.length)
        if (n === 0 || chart.maxValue <= 0) return
        // The newest sample is the right edge, so the GUI clock does not matter.
        const start = chart.times[n - 1] - chart.windowMs
        // Split into continuous segments.
        const segments = []
        let current = []
        for (let i = 0; i < n; ++i) {
            const valid = chart.values[i] !== null && chart.values[i] !== undefined && chart.values[i] >= 0 && chart.times[i] >= start
            const gap = current.length > 0 && chart.times[i] - chart.times[current[current.length - 1]] > chart.gapMs
            if (!valid || gap) {
                if (current.length > 0) segments.push(current)
                current = []
            }
            if (valid) current.push(i)
        }
        if (current.length > 0) segments.push(current)
        const line = chart.color
        for (const seg of segments) {
            ctx.beginPath()
            const first = point(seg[0], start)
            ctx.moveTo(first.x, height)
            for (const i of seg) { const p = point(i, start); ctx.lineTo(p.x, p.y) }
            const last = point(seg[seg.length - 1], start)
            ctx.lineTo(last.x, height)
            ctx.closePath()
            ctx.fillStyle = Qt.rgba(line.r, line.g, line.b, 0.16)
            ctx.fill()
            ctx.beginPath()
            ctx.strokeStyle = line
            ctx.lineWidth = 1.6
            ctx.lineJoin = "round"
            for (let k = 0; k < seg.length; ++k) {
                const p = point(seg[k], start)
                if (k === 0) ctx.moveTo(p.x, p.y)
                else ctx.lineTo(p.x, p.y)
            }
            if (seg.length === 1) { ctx.lineTo(first.x + 1, first.y) }
            ctx.stroke()
        }
    }

    Text {
        anchors.centerIn: parent
        visible: chart.emptyText.length > 0
        text: chart.emptyText
        color: "#64748b"
        font.pixelSize: 11
    }
}
