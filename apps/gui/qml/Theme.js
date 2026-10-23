.pragma library
function stateColor(state) {
    switch (state) {
    case "RUNNING": return "#34d399"
    case "STARTING": case "STOPPING": case "RESTARTING": return "#fbbf24"
    case "UNHEALTHY": return "#fb923c"
    case "BACKOFF": return "#a78bfa"
    case "FAILED": return "#f87171"
    default: return "#94a3b8"
    }
}
function fileName(path) {
    if (!path) return ""
    const parts = String(path).split(/[\\/]/)
    return parts[parts.length - 1]
}
function duration(seconds) {
    seconds = Math.max(0, Math.floor(seconds || 0))
    const h = Math.floor(seconds / 3600), m = Math.floor(seconds % 3600 / 60), s = seconds % 60
    if (h > 0) return h + "h " + m + "m"
    if (m > 0) return m + "m " + s + "s"
    return s + "s"
}
function clock(iso) {
    if (!iso) return ""
    const d = new Date(iso)
    const pad = n => (n < 10 ? "0" : "") + n
    return pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" + pad(d.getSeconds())
}
function localTime(iso) {
    if (!iso) return ""
    const d = new Date(iso)
    const pad = n => (n < 10 ? "0" : "") + n
    return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) + " "
        + pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" + pad(d.getSeconds())
}
// Chart colors for the resource dashboard (distinct from the state colors above).
var cpuColor = "#38bdf8"
var ramColor = "#818cf8"
var gpuColor = "#2dd4bf"
var vramColor = "#f472b6"
// Formats a byte count as GB/MB; negative or missing values mean "not available".
function bytes(value) {
    if (value === undefined || value === null || value < 0) return "—"
    const gb = value / (1024 * 1024 * 1024)
    if (gb >= 1) return (gb >= 100 ? gb.toFixed(0) : gb.toFixed(1)) + " GB"
    return (value / (1024 * 1024)).toFixed(0) + " MB"
}
function percent(value) {
    if (value === undefined || value === null || value < 0) return "—"
    return (value >= 10 ? value.toFixed(0) : value.toFixed(1)) + "%"
}
function available(value) {
    return value !== undefined && value !== null && value >= 0
}
