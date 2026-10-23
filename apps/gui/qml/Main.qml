import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1120; height: 850
    minimumWidth: 840; minimumHeight: 650
    title: "Llama Server Manager · Phase 1"
    palette.window: "#111827"
    palette.base: "#1f2937"
    palette.alternateBase: "#263449"
    palette.text: "#e5e7eb"
    palette.windowText: "#e5e7eb"
    palette.button: "#334155"
    palette.buttonText: "#f8fafc"
    palette.highlight: "#2563eb"
    readonly property string serverState: agent.status.state || "UNKNOWN"
    readonly property bool idle: serverState === "STOPPED" || serverState === "FAILED"
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 20; spacing: 12
        RowLayout {
            Label { text: "Llama Server Manager"; font.pixelSize: 26; font.bold: true; Layout.fillWidth: true }
            Label { text: agent.connected ? "● Agent 已連線" : "● Agent 重新連線中"; color: agent.connected ? "#34d399" : "#fbbf24" }
        }
        Label { text: "單一模型管理 · 關閉視窗後，Agent 與模型仍會持續執行"; color: "#94a3b8" }
        Frame {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                Label { text: root.serverState; font.bold: true; font.pixelSize: 20; Layout.fillWidth: true }
                Label { text: "PID  " + (agent.status.pid || "—") }
                Label { text: "Port  " + (agent.status.port || "—") }
                Label { text: "執行  " + (agent.status.uptimeSeconds || 0) + " s" }
                Button { text: "啟動"; enabled: agent.connected && root.idle && !agent.busy && !editor.dirty; onClicked: agent.request("startServer") }
                Button { text: "停止"; enabled: agent.connected && !root.idle && root.serverState !== "STOPPING"; onClicked: agent.request("stopServer") }
                Button { text: "重新啟動"; enabled: agent.connected && root.serverState === "RUNNING" && !agent.busy; onClicked: agent.request("restartServer") }
            }
        }
        Label { text: "RUNNING 表示程序已啟動；模型載入進度請查看日誌。Phase 1 不執行 HTTP 健康檢查。"; color: "#94a3b8"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label { text: agent.error || agent.status.error || ""; visible: text.length > 0; color: "#fca5a5"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        SplitView {
            orientation: Qt.Vertical; Layout.fillWidth: true; Layout.fillHeight: true
            ScrollView {
                SplitView.preferredHeight: 360; SplitView.minimumHeight: 120
                contentWidth: availableWidth
                ProfileEditor { id: editor; width: parent.width; editable: agent.connected && root.idle && !agent.busy }
            }
            LogView { SplitView.fillHeight: true; SplitView.minimumHeight: 150 }
        }
    }
}
