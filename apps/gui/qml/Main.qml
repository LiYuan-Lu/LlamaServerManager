import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1120; height: 850
    minimumWidth: 840; minimumHeight: 650
    title: qsTr("Llama Server Manager")
    palette.window: "#111827"
    palette.base: "#1f2937"
    palette.alternateBase: "#263449"
    palette.text: "#e5e7eb"
    palette.windowText: "#e5e7eb"
    palette.button: "#334155"
    palette.buttonText: "#f8fafc"
    palette.highlight: "#2563eb"

    readonly property int runningCount: {
        let n = 0
        for (const id of agent.instanceIds) if ((agent.instances[id] || {}).state === "RUNNING") ++n
        return n
    }
    function nextPort() {
        let port = 8079
        for (const id of agent.instanceIds) port = Math.max(port, ((agent.instances[id] || {}).profile || {}).port || 0)
        return Math.min(port + 1, 65535)
    }
    function openModel(id) {
        stack.push(modelPage, {profileId: id, suggestedPort: nextPort()})
    }

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
        Label {
            text: qsTr("%1 of %2 models running · The Agent and models keep running after the window is closed")
                .arg(root.runningCount).arg(agent.instanceIds.length)
            color: "#94a3b8"
        }
        Label { text: agent.error || ""; visible: text.length > 0; color: "#fca5a5"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label { text: agent.warning || ""; visible: text.length > 0; color: "#fbbf24"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        StackView {
            id: stack
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            initialItem: gridPage
        }
    }

    Component {
        id: gridPage
        ScrollView {
            id: scroll
            contentWidth: availableWidth
            contentHeight: flow.height
            Flow {
                id: flow
                width: scroll.availableWidth
                spacing: 14
                readonly property int columns: Math.max(1, Math.floor((width + spacing) / (300 + spacing)))
                readonly property real cardWidth: Math.max(240, Math.floor((width - spacing * (columns - 1)) / columns))
                Repeater {
                    model: agent.instanceIds
                    ModelCard { width: flow.cardWidth; profileId: modelData; onOpen: root.openModel(modelData) }
                }
                AddCard { width: flow.cardWidth; onClicked: root.openModel("") }
                Label {
                    visible: agent.synchronized && agent.instanceIds.length === 0
                    width: flow.width
                    text: qsTr("No models yet. Click + to add your first model.")
                    color: "#94a3b8"
                }
            }
        }
    }
    Component {
        id: modelPage
        ModelPage { onBack: { agent.selectedId = ""; agent.clearError(); stack.pop() } }
    }
}
