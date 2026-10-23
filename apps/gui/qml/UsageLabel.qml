import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

// Resource usage of one model's llama-server process: CPU, RAM (working set), and the
// process's own GPU memory when the driver reports it (not the whole-card total).
Label {
    id: usage
    property string profileId: ""
    readonly property var process: ((agent.metrics || {}).processes || {})[profileId] || null
    readonly property bool gpuAvailable: ((agent.metrics || {}).gpu || {}).available === true
    readonly property bool running: process !== null && process.running === true
    readonly property string vramText: {
        if (!running || !gpuAvailable) return ""
        if (process.gpuMemoryBytes < 0) return qsTr("VRAM n/a")
        return qsTr("VRAM %1").arg(Theme.bytes(process.gpuMemoryBytes))
    }
    visible: running
    text: running ? [qsTr("CPU %1").arg(Theme.percent(process.cpuPercent)), qsTr("RAM %1").arg(Theme.bytes(process.ramBytes)), vramText]
        .filter(t => t.length > 0).join("  ·  ") : ""
    color: "#cbd5e1"
    font.pixelSize: 12
    elide: Text.ElideRight
    ToolTip.visible: hover.hovered && running
    ToolTip.text: process && process.gpuMemoryBytes < 0 && gpuAvailable
        ? qsTr("Usage of this model's process. The GPU driver does not report per-process VRAM (common with Windows WDDM drivers); see the dashboard for whole-GPU usage.")
        : qsTr("Usage of this model's process. CPU is relative to all logical processors; RAM is the working set.")
    HoverHandler { id: hover }
}
