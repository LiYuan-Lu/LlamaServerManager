import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

// Watchdog restart history of the selected model, newest first.
Item {
    id: view
    function kindText(kind) {
        return kind === "crash" ? qsTr("Crash") : kind === "startup" ? qsTr("Startup failure") : kind === "health" ? qsTr("Health check") : kind
    }
    function outcomeText(outcome) {
        switch (outcome) {
        case "recovered": return qsTr("Recovered")
        case "failed": return qsTr("Restart failed")
        case "pending": return qsTr("Restart pending")
        case "gave up: restart limit reached": return qsTr("Gave up: restart limit reached")
        case "cancelled by manual stop": return qsTr("Cancelled by manual stop")
        case "superseded by manual restart": return qsTr("Superseded by manual restart")
        case "auto restart disabled": return qsTr("Auto restart disabled")
        case "interrupted by Agent restart": return qsTr("Interrupted by Agent restart")
        default: return outcome
        }
    }
    function outcomeColor(outcome) {
        if (outcome === "recovered") return "#34d399"
        if (outcome === "pending") return "#fbbf24"
        if (outcome === "failed" || outcome.indexOf("gave up") === 0) return "#f87171"
        return "#94a3b8"
    }
    Label {
        anchors.centerIn: parent
        visible: agent.restartHistory.length === 0
        text: qsTr("No failures recorded. Crashes, startup failures, and failed health checks will appear here.")
        color: "#94a3b8"
    }
    ListView {
        id: list
        anchors.fill: parent
        clip: true; spacing: 6
        model: agent.restartHistory.slice().reverse()
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            width: list.width - 12
            height: row.implicitHeight + 16
            radius: 6; color: "#1f2937"
            ColumnLayout {
                id: row
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    Label { text: Theme.localTime(modelData.time); color: "#cbd5e1"; font.family: "Consolas" }
                    Label { text: view.kindText(modelData.kind); font.bold: true }
                    Label { text: modelData.attempt > 0 ? qsTr("Attempt %1").arg(modelData.attempt) : ""; color: "#94a3b8" }
                    Label { text: modelData.pid ? qsTr("PID %1").arg(modelData.pid) : ""; color: "#94a3b8" }
                    Item { Layout.fillWidth: true }
                    Label { text: view.outcomeText(modelData.outcome); color: view.outcomeColor(modelData.outcome); font.bold: true }
                }
                Label { text: modelData.reason; color: "#e5e7eb"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }
}
