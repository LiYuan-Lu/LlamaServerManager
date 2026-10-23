import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

Rectangle {
    id: card
    property string profileId: ""
    readonly property var info: agent.instances[profileId] || ({})
    readonly property var profile: info.profile || ({})
    readonly property string serverState: info.state || "STOPPED"
    signal open()
    function healthText(health) {
        if (!health) return "—"
        if (health === "Healthy") return qsTr("Healthy")
        if (health === "Disabled") return qsTr("Health checks off")
        if (health === "Not running") return qsTr("Not running")
        if (health.indexOf("Loading model") === 0) return qsTr("Loading model (HTTP 503)")
        if (health.indexOf("Waiting for /health") === 0) return qsTr("Waiting for /health")
        return health
    }
    implicitHeight: 216
    radius: 10
    color: hover.hovered ? "#243244" : "#1f2937"
    border.color: serverState === "RUNNING" ? "#065f46" : serverState === "FAILED" ? "#7f1d1d"
        : serverState === "UNHEALTHY" || serverState === "BACKOFF" || serverState === "RESTARTING" ? "#9a3412" : "#334155"
    readonly property string watchdogText: {
        const s = serverState, i = info
        let text = ""
        if (s === "RESTARTING" || s === "BACKOFF")
            text = i.nextRestartSeconds > 0 ? qsTr("Restart %1/%2 in %3 s").arg(i.restartAttempt).arg(i.maxRestarts).arg(i.nextRestartSeconds)
                                            : qsTr("Restarting (attempt %1/%2)…").arg(i.restartAttempt).arg(i.maxRestarts)
        else if (s === "STARTING") text = (profile.watchdog || {}).healthCheck === false ? qsTr("Starting…") : qsTr("Loading model…")
        else if (s === "RUNNING") text = i.health === "Healthy" ? "✓ " + qsTr("Healthy") : card.healthText(i.health)
        if ((profile.watchdog || {}).autoRestart === false) text += (text ? "  ·  " : "") + qsTr("Auto restart off")
        if (profile.autoStart === true) text += (text ? "  ·  " : "") + qsTr("Starts with Agent")
        return text
    }
    border.width: 1
    // Generation speed from the Agent's /slots sampling; shown while the process runs.
    readonly property var throughput: info.throughput || ({})
    readonly property bool generating: throughput.available === true && throughput.activeSlots > 0
    readonly property string speedText: {
        if (throughput.available !== true)
            return (throughput.reason || "").indexOf("disabled") >= 0 ? qsTr("Speed off") : qsTr("Speed —")
        return generating ? qsTr("%1 tok/s").arg(Number(throughput.tokensPerSecond).toFixed(1)) : qsTr("Idle")
    }
    readonly property string speedDetail: throughput.available === true && throughput.lastTokensPerSecond > 0
        ? qsTr("last %1 tok/s").arg(Number(throughput.lastTokensPerSecond).toFixed(1)) : ""

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: card.open() }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 14; spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: card.info.name || qsTr("Untitled model")
                font.pixelSize: 17; font.bold: true
                elide: Text.ElideRight; Layout.fillWidth: true
            }
            StateBadge { serverState: card.serverState }
        }
        Label {
            text: Theme.fileName(card.profile.model) || qsTr("No model file selected")
            color: "#94a3b8"; elide: Text.ElideMiddle; Layout.fillWidth: true
        }
        Label {
            text: ("%1:%2").arg(card.profile.host || "127.0.0.1").arg(card.profile.port || "—")
                + (card.info.pid ? qsTr("  ·  PID %1").arg(card.info.pid) : "")
                + (card.serverState === "RUNNING" ? qsTr("  ·  up %1").arg(Theme.duration(card.info.uptimeSeconds)) : "")
            color: "#cbd5e1"; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
        }
        UsageLabel { profileId: card.profileId; Layout.fillWidth: true }
        RowLayout {
            visible: card.info.pid > 0 && (card.serverState === "RUNNING" || card.serverState === "UNHEALTHY")
            Layout.fillWidth: true; spacing: 6
            Label {
                text: card.speedText
                color: card.generating ? "#c4b5fd" : "#94a3b8"
                font.pixelSize: 13; font.bold: card.generating
            }
            Label {
                text: card.speedDetail
                visible: text.length > 0
                color: "#94a3b8"; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
            }
            Item { Layout.fillWidth: true; visible: card.speedDetail.length === 0 }
        }
        Label {
            text: card.watchdogText
            visible: text.length > 0
            color: card.serverState === "RUNNING" && card.info.health === "Healthy" ? "#6ee7b7" : "#cbd5e1"
            font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
        }
        Label {
            text: ["FAILED", "UNHEALTHY", "BACKOFF", "RESTARTING"].indexOf(card.serverState) >= 0 ? (card.info.error || "") : ""
            visible: text.length > 0
            color: "#fca5a5"; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
        }
        Item { Layout.fillHeight: true }
        ServerControls { Layout.fillWidth: true; profileId: card.profileId; info: card.info }
    }
}
