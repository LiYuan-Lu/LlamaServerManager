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
function localTime(iso) {
    if (!iso) return ""
    const d = new Date(iso)
    const pad = n => (n < 10 ? "0" : "") + n
    return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) + " "
        + pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" + pad(d.getSeconds())
}
