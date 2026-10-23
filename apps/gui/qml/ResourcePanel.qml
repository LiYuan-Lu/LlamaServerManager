import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

// System-wide dashboard: CPU, RAM, GPU, and VRAM with 5-minute charts.
// GPU values are for the whole card (all processes); per-model usage is on the model cards.
ColumnLayout {
    id: panel
    spacing: 8
    readonly property var latest: agent.metrics || ({})
    readonly property var system: latest.system || ({})
    readonly property var gpu: latest.gpu || ({})
    readonly property var devices: gpu.devices || []
    readonly property bool gpuAvailable: gpu.available === true && devices.length > 0
    property int gpuIndex: 0
    readonly property var device: devices[Math.min(gpuIndex, devices.length - 1)] || ({})
    readonly property int gapMs: Math.max(5000, agent.metricsInterval * 3000)
    // Known Agent reasons (see GpuMonitor) are translated; other NVML errors are shown as sent.
    function gpuReason(reason) {
        if (!reason || reason === "No NVIDIA GPU found") return qsTr("No NVIDIA GPU detected")
        if (reason.indexOf("NVML not found") === 0) return qsTr("NVIDIA driver (NVML) not found")
        if (reason.indexOf("NVML initialization failed") === 0) return qsTr("NVIDIA driver could not be initialized")
        return reason
    }

    // Parallel arrays for the charts, rebuilt once per sample.
    readonly property var series: {
        const history = agent.metricsHistory
        const out = { times: [], cpu: [], ram: [], gpu: [], vram: [] }
        for (const p of history) {
            out.times.push(p.time)
            out.cpu.push(p.cpu)
            out.ram.push(p.ramTotal > 0 && p.ramUsed >= 0 ? p.ramUsed / p.ramTotal * 100 : -1)
            const g = (p.gpus || [])[panel.gpuIndex]
            out.gpu.push(g ? g.util : -1)
            out.vram.push(g && g.memTotal > 0 && g.memUsed >= 0 ? g.memUsed / g.memTotal * 100 : -1)
        }
        return out
    }

    RowLayout {
        Layout.fillWidth: true; spacing: 8
        Label { text: qsTr("System resources"); font.bold: true; font.pixelSize: 15 }
        Label {
            text: !agent.connected ? qsTr("Waiting for the Agent…") : panel.latest.time ? qsTr("Updated %1").arg(Theme.clock(panel.latest.time)) : qsTr("Collecting…")
            color: "#64748b"; font.pixelSize: 12
        }
        Item { Layout.fillWidth: true }
        Label { text: qsTr("GPU"); visible: panel.devices.length > 1; color: "#94a3b8" }
        ComboBox {
            visible: panel.devices.length > 1
            model: panel.devices.map(d => d.index + ": " + d.name)
            currentIndex: panel.gpuIndex
            onActivated: index => panel.gpuIndex = index
            implicitWidth: 220
        }
        Label { text: qsTr("Refresh every"); color: "#94a3b8" }
        ComboBox {
            id: interval
            readonly property var choices: [1, 2, 5, 10]
            model: choices.map(s => qsTr("%1 s").arg(s))
            currentIndex: Math.max(0, choices.indexOf(agent.metricsInterval))
            enabled: agent.connected
            onActivated: index => agent.setMetricsInterval(choices[index])
            implicitWidth: 86
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: panel.width > 900 ? 4 : 2
        columnSpacing: 12; rowSpacing: 12
        MetricTile {
            Layout.fillWidth: true
            title: qsTr("CPU")
            accent: Theme.cpuColor
            value: Theme.percent(panel.system.cpuPercent)
            detail: panel.system.logicalProcessors ? qsTr("%1 logical processors · all processes").arg(panel.system.logicalProcessors) : ""
            times: panel.series.times; values: panel.series.cpu; maxValue: 100; gapMs: panel.gapMs
        }
        MetricTile {
            Layout.fillWidth: true
            title: qsTr("RAM")
            accent: Theme.ramColor
            value: Theme.bytes(panel.system.ramUsedBytes)
            detail: Theme.available(panel.system.ramTotalBytes) ? qsTr("of %1 · %2 in use").arg(Theme.bytes(panel.system.ramTotalBytes))
                .arg(Theme.percent(panel.system.ramUsedBytes / panel.system.ramTotalBytes * 100)) : ""
            times: panel.series.times; values: panel.series.ram; maxValue: 100; gapMs: panel.gapMs
        }
        MetricTile {
            Layout.fillWidth: true
            title: panel.gpuAvailable && panel.device.name ? qsTr("GPU · %1").arg(panel.device.name) : qsTr("GPU")
            accent: Theme.gpuColor
            value: panel.gpuAvailable ? Theme.percent(panel.device.utilizationPercent) : panel.latest.time ? qsTr("Not available") : "—"
            detail: {
                if (!panel.gpuAvailable) return panel.latest.time ? panel.gpuReason(panel.gpu.reason) : ""
                const parts = []
                if (Theme.available(panel.device.temperatureC)) parts.push(qsTr("%1 °C").arg(panel.device.temperatureC))
                if (Theme.available(panel.device.powerWatts)) parts.push(qsTr("%1 W").arg(Math.round(panel.device.powerWatts)))
                parts.push(qsTr("whole GPU"))
                return parts.join(" · ")
            }
            times: panel.series.times; values: panel.series.gpu; maxValue: 100; gapMs: panel.gapMs
            emptyText: panel.latest.time && !panel.gpuAvailable ? qsTr("GPU monitoring unavailable") : ""
        }
        MetricTile {
            Layout.fillWidth: true
            title: qsTr("VRAM")
            accent: Theme.vramColor
            value: panel.gpuAvailable ? Theme.bytes(panel.device.memoryUsedBytes) : panel.latest.time ? qsTr("Not available") : "—"
            detail: panel.gpuAvailable && Theme.available(panel.device.memoryTotalBytes)
                ? qsTr("of %1 · all processes on this GPU").arg(Theme.bytes(panel.device.memoryTotalBytes)) : ""
            times: panel.series.times; values: panel.series.vram; maxValue: 100; gapMs: panel.gapMs
            emptyText: panel.latest.time && !panel.gpuAvailable ? qsTr("GPU monitoring unavailable") : ""
        }
    }
}
