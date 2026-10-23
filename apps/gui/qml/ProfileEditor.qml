import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: editor
    property string profileId: ""
    property int suggestedPort: 8080
    // Prefills an unsaved model (Duplicate); saved models always load their own profile.
    property var template: null
    property bool editable: false
    property bool dirty: false
    // Set while a create/update request is in flight so the page can follow a new model's ID.
    property bool savePending: false
    property string validationError: ""
    spacing: 8
    function load() {
        const entry = agent.instances[profileId]
        const p = entry ? (entry.profile || {}) : (template || {})
        name.text = p.name || ""
        exe.text = p.executable || ""; model.text = p.model || ""; cwd.text = p.workingDirectory || ""
        host.text = p.host || "127.0.0.1"; port.text = String(entry ? (p.port || suggestedPort) : suggestedPort)
        context.text = String(p.context || 4096); gpu.text = String(p.gpuLayers === undefined ? 0 : p.gpuLayers)
        threads.text = String(p.threads || 4); parallel.text = String(p.parallel || 1)
        metrics.checked = p.metrics || false
        autoStartBox.checked = p.autoStart === true
        extra.text = JSON.stringify(p.extraArguments || []); env.text = JSON.stringify(p.environment || {})
        const w = p.watchdog || {}
        autoRestart.checked = w.autoRestart !== false; healthCheck.checked = w.healthCheck !== false
        wdInterval.text = String(w.intervalSeconds || 30); wdTimeout.text = String(w.timeoutSeconds || 5)
        wdThreshold.text = String(w.failureThreshold || 3); wdGrace.text = String(w.startupGraceSeconds || 600)
        wdCooldown.text = String(w.cooldownSeconds === undefined ? 5 : w.cooldownSeconds); wdBackoff.text = String(w.maxBackoffSeconds || 300)
        wdMax.text = String(w.maxRestarts === undefined ? 3 : w.maxRestarts); wdWindow.text = String(w.windowSeconds || 600)
        dirty = false; validationError = ""
    }
    function save() {
        try {
            const args = JSON.parse(extra.text), variables = JSON.parse(env.text)
            if (!Array.isArray(args) || typeof variables !== "object" || variables === null || Array.isArray(variables)) throw new Error(qsTr("Extra arguments must be a JSON array, and environment variables must be a JSON object"))
            const values = [port.text, context.text, gpu.text, threads.text, parallel.text].map(Number)
            const wd = [wdInterval.text, wdTimeout.text, wdThreshold.text, wdGrace.text, wdCooldown.text, wdBackoff.text, wdMax.text, wdWindow.text].map(Number)
            if (values.concat(wd).some(v => !Number.isInteger(v))) throw new Error(qsTr("Numeric fields must be integers"))
            if (name.text.trim().length === 0) throw new Error(qsTr("Enter a name for this model"))
            validationError = ""
            const profile = {name: name.text.trim(), executable: exe.text, model: model.text, workingDirectory: cwd.text,
                host: host.text, port: values[0], context: values[1], gpuLayers: values[2], threads: values[3], parallel: values[4],
                metrics: metrics.checked, autoStart: autoStartBox.checked, extraArguments: args, environment: variables,
                watchdog: {autoRestart: autoRestart.checked, healthCheck: healthCheck.checked,
                    intervalSeconds: wd[0], timeoutSeconds: wd[1], failureThreshold: wd[2], startupGraceSeconds: wd[3],
                    cooldownSeconds: wd[4], maxBackoffSeconds: wd[5], maxRestarts: wd[6], windowSeconds: wd[7]}}
            savePending = true
            if (profileId === "") agent.createProfile(profile)
            else agent.saveProfile(profileId, profile)
        } catch (e) { validationError = String(e) }
    }
    Connections {
        target: agent
        function onProfileChanged(id) { if (id === editor.profileId) { editor.savePending = false; editor.load() } }
        function onBusyChanged() { if (!agent.busy) editor.savePending = false }
    }
    onProfileIdChanged: load()
    Component.onCompleted: load()
    FileDialog { id: exeDialog; title: qsTr("Select llama-server.exe"); nameFilters: ["Executable (*.exe)"]; onAccepted: { exe.text = agent.localPath(selectedFile); editor.dirty = true } }
    FileDialog { id: modelDialog; title: qsTr("Select GGUF model"); nameFilters: ["GGUF models (*.gguf)"]; onAccepted: {
        model.text = agent.localPath(selectedFile)
        if (name.text.trim().length === 0) name.text = model.text.split(/[\\/]/).pop().replace(/\.gguf$/i, "")
        editor.dirty = true
    } }
    FolderDialog { id: cwdDialog; title: qsTr("Select working directory"); onAccepted: { cwd.text = agent.localPath(selectedFolder); editor.dirty = true } }
    GridLayout {
        columns: 3; Layout.fillWidth: true; enabled: editor.editable
        Label { text: qsTr("Name") }
        TextField { id: name; Layout.fillWidth: true; Layout.columnSpan: 2; maximumLength: 128; placeholderText: qsTr("Shown on the model card"); onTextEdited: editor.dirty = true }
        Label { text: "llama-server.exe" }
        TextField { id: exe; Layout.fillWidth: true; placeholderText: qsTr("Select the executable"); onTextEdited: editor.dirty = true }
        Button { text: qsTr("Browse…"); onClicked: exeDialog.open() }
        Label { text: qsTr("GGUF model") }
        TextField { id: model; Layout.fillWidth: true; placeholderText: qsTr("Select the model file"); onTextEdited: editor.dirty = true }
        Button { text: qsTr("Browse…"); onClicked: modelDialog.open() }
        Label { text: qsTr("Working directory") }
        TextField { id: cwd; Layout.fillWidth: true; placeholderText: qsTr("Leave empty to use the executable directory"); onTextEdited: editor.dirty = true }
        Button { text: qsTr("Browse…"); onClicked: cwdDialog.open() }
    }
    GridLayout {
        columns: 8; Layout.fillWidth: true; enabled: editor.editable
        Label { text: "Host" }
        TextField { id: host; Layout.fillWidth: true; onTextEdited: editor.dirty = true }
        Label { text: "Port" }
        TextField { id: port; Layout.preferredWidth: 75; validator: IntValidator { bottom: 1; top: 65535 } onTextEdited: editor.dirty = true }
        Label { text: "Context" }
        TextField { id: context; Layout.preferredWidth: 85; validator: IntValidator { bottom: 1 } onTextEdited: editor.dirty = true }
        Label { text: "GPU layers" }
        TextField { id: gpu; Layout.preferredWidth: 65; validator: IntValidator { bottom: -1 } onTextEdited: editor.dirty = true }
        Label { text: "Threads" }
        TextField { id: threads; Layout.preferredWidth: 65; validator: IntValidator { bottom: 1 } onTextEdited: editor.dirty = true }
        Label { text: "Parallel" }
        TextField { id: parallel; Layout.preferredWidth: 65; validator: IntValidator { bottom: 1 } onTextEdited: editor.dirty = true }
        CheckBox { id: metrics; text: "--metrics"; Layout.columnSpan: 4; onClicked: editor.dirty = true }
    }
    RowLayout {
        Layout.fillWidth: true; enabled: editor.editable
        Label { text: qsTr("Extra arguments (JSON)") }
        TextField { id: extra; Layout.fillWidth: true; placeholderText: '["--verbose"]'; onTextEdited: editor.dirty = true }
        Label { text: qsTr("Environment variables (JSON)") }
        TextField { id: env; Layout.fillWidth: true; placeholderText: '{"NAME":"value"}'; onTextEdited: editor.dirty = true }
    }
    CheckBox {
        id: autoStartBox
        enabled: editor.editable
        text: qsTr("Start automatically when the Agent starts")
        onClicked: editor.dirty = true
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Each time the Agent starts, it starts this model too. Models stopped with Stop stay stopped until the next Agent start.")
    }
    GroupBox {
        title: qsTr("Watchdog")
        Layout.fillWidth: true; enabled: editor.editable
        ColumnLayout {
            anchors.fill: parent; spacing: 6
            RowLayout {
                CheckBox { id: autoRestart; text: qsTr("Restart automatically after crashes, startup failures, and failed health checks"); onClicked: editor.dirty = true }
            }
            RowLayout {
                CheckBox { id: healthCheck; text: qsTr("Check GET /health (STARTING lasts until it returns 200)"); onClicked: editor.dirty = true }
            }
            GridLayout {
                columns: 8; Layout.fillWidth: true
                Label { text: qsTr("Interval (s)") }
                TextField { id: wdInterval; Layout.preferredWidth: 70; enabled: healthCheck.checked; validator: IntValidator { bottom: 1; top: 3600 } onTextEdited: editor.dirty = true }
                Label { text: qsTr("Timeout (s)") }
                TextField { id: wdTimeout; Layout.preferredWidth: 70; enabled: healthCheck.checked; validator: IntValidator { bottom: 1; top: 300 } onTextEdited: editor.dirty = true }
                Label { text: qsTr("Failures before unhealthy") }
                TextField { id: wdThreshold; Layout.preferredWidth: 70; enabled: healthCheck.checked; validator: IntValidator { bottom: 1; top: 100 } onTextEdited: editor.dirty = true }
                Label { text: qsTr("Startup grace (s)") }
                TextField { id: wdGrace; Layout.preferredWidth: 70; enabled: healthCheck.checked; validator: IntValidator { bottom: 1; top: 86400 } onTextEdited: editor.dirty = true }
                Label { text: qsTr("Restart cooldown (s)") }
                TextField { id: wdCooldown; Layout.preferredWidth: 70; enabled: autoRestart.checked; validator: IntValidator { bottom: 0; top: 3600 } onTextEdited: editor.dirty = true }
                Label { text: qsTr("Max backoff (s)") }
                TextField { id: wdBackoff; Layout.preferredWidth: 70; enabled: autoRestart.checked; validator: IntValidator { bottom: 0; top: 86400 } onTextEdited: editor.dirty = true }
                Label { text: qsTr("Max restarts") }
                TextField { id: wdMax; Layout.preferredWidth: 70; enabled: autoRestart.checked; validator: IntValidator { bottom: 0; top: 100 } onTextEdited: editor.dirty = true }
                Label { text: qsTr("Within (s)") }
                TextField { id: wdWindow; Layout.preferredWidth: 70; enabled: autoRestart.checked; validator: IntValidator { bottom: 1; top: 86400 } onTextEdited: editor.dirty = true }
            }
        }
    }
    // The Save/Revert bar and validation errors are shown by ModelPage, outside the scroll area.
}
