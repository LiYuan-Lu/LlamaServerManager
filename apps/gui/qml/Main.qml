import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1120; height: 850
    minimumWidth: 840; minimumHeight: 650
    title: qsTr("Llama Server Manager · Phase 1")
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
            Label {
                text: agent.connected ? qsTr("● Agent connected") : qsTr("● Agent reconnecting")
                color: agent.connected ? "#34d399" : "#fbbf24"
                Layout.rightMargin: 12
            }
            Button {
                id: langButton
                // Language names are shown in their own language and are not translated.
                text: languageManager.language === "zh_TW" ? "EN" : "中文"
                Layout.leftMargin: 8
                Layout.preferredHeight: 34
                implicitWidth: 64
                flat: true
                onClicked: languageManager.setLanguage(languageManager.language === "zh_TW" ? "en" : "zh_TW")
            }
        }
        Label { text: qsTr("Single model management · The Agent and model keep running after the window is closed"); color: "#94a3b8" }
        Frame {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                Label { text: root.serverState; font.bold: true; font.pixelSize: 20; Layout.fillWidth: true }
                Label { text: qsTr("PID  %1").arg(agent.status.pid || "—") }
                Label { text: qsTr("Port  %1").arg(agent.status.port || "—") }
                Label { text: qsTr("Uptime  %1 s").arg(agent.status.uptimeSeconds || 0) }
                Button {
                    text: qsTr("Start")
                    enabled: agent.connected && root.idle && !agent.busy && !editor.dirty
                    onClicked: agent.request("startServer")
                }
                Button {
                    text: qsTr("Stop")
                    enabled: agent.connected && !root.idle && root.serverState !== "STOPPING"
                    onClicked: agent.request("stopServer")
                }
                Button {
                    text: qsTr("Restart")
                    enabled: agent.connected && root.serverState === "RUNNING" && !agent.busy
                    onClicked: agent.request("restartServer")
                }
            }
        }
        Label {
            text: qsTr("RUNNING means the process has started; check the logs for model loading progress. Phase 1 does not perform HTTP health checks.")
            color: "#94a3b8"; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
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
