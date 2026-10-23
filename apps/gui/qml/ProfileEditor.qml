import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: editor
    property bool editable: false
    property bool dirty: false
    property string validationError: ""
    spacing: 8
    function load() {
        const p = agent.profile
        exe.text = p.executable || ""; model.text = p.model || ""; cwd.text = p.workingDirectory || ""
        host.text = p.host || "127.0.0.1"; port.text = String(p.port || 8080)
        context.text = String(p.context || 4096); gpu.text = String(p.gpuLayers === undefined ? 0 : p.gpuLayers)
        threads.text = String(p.threads || 4); parallel.text = String(p.parallel || 1)
        metrics.checked = p.metrics || false
        extra.text = JSON.stringify(p.extraArguments || []); env.text = JSON.stringify(p.environment || {})
        dirty = false; validationError = ""
    }
    function save() {
        try {
            const args = JSON.parse(extra.text), variables = JSON.parse(env.text)
            if (!Array.isArray(args) || typeof variables !== "object" || variables === null || Array.isArray(variables)) throw new Error("額外參數必須是 JSON 陣列，環境變數必須是 JSON 物件")
            const values = [port.text, context.text, gpu.text, threads.text, parallel.text].map(Number)
            if (values.some(v => !Number.isInteger(v))) throw new Error("數值欄位必須為整數")
            validationError = ""
            agent.saveProfile({executable: exe.text, model: model.text, workingDirectory: cwd.text,
                host: host.text, port: values[0], context: values[1], gpuLayers: values[2], threads: values[3], parallel: values[4],
                metrics: metrics.checked, extraArguments: args, environment: variables})
        } catch (e) { validationError = String(e) }
    }
    Connections { target: agent; function onProfileChanged() { editor.load() } }
    Component.onCompleted: load()
    FileDialog { id: exeDialog; title: "選擇 llama-server.exe"; nameFilters: ["Executable (*.exe)"]; onAccepted: { exe.text = agent.localPath(selectedFile); editor.dirty = true } }
    FileDialog { id: modelDialog; title: "選擇 GGUF 模型"; nameFilters: ["GGUF models (*.gguf)"]; onAccepted: { model.text = agent.localPath(selectedFile); editor.dirty = true } }
    FolderDialog { id: cwdDialog; title: "選擇工作目錄"; onAccepted: { cwd.text = agent.localPath(selectedFolder); editor.dirty = true } }
    GridLayout {
        columns: 3; Layout.fillWidth: true; enabled: editor.editable
        Label { text: "llama-server.exe" }
        TextField { id: exe; Layout.fillWidth: true; placeholderText: "選擇執行檔"; onTextEdited: editor.dirty = true }
        Button { text: "瀏覽…"; onClicked: exeDialog.open() }
        Label { text: "GGUF 模型" }
        TextField { id: model; Layout.fillWidth: true; placeholderText: "選擇模型檔案"; onTextEdited: editor.dirty = true }
        Button { text: "瀏覽…"; onClicked: modelDialog.open() }
        Label { text: "工作目錄" }
        TextField { id: cwd; Layout.fillWidth: true; placeholderText: "留空使用執行檔目錄"; onTextEdited: editor.dirty = true }
        Button { text: "瀏覽…"; onClicked: cwdDialog.open() }
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
        Label { text: "額外參數 (JSON)" }
        TextField { id: extra; Layout.fillWidth: true; placeholderText: '["--verbose"]'; onTextEdited: editor.dirty = true }
        Label { text: "環境變數 (JSON)" }
        TextField { id: env; Layout.fillWidth: true; placeholderText: '{"NAME":"value"}'; onTextEdited: editor.dirty = true }
    }
    RowLayout {
        Button { text: "儲存設定"; enabled: editor.editable; onClicked: editor.save() }
        Button { text: "還原編輯"; enabled: editor.editable; onClicked: editor.load() }
        Label { text: editor.dirty ? "尚未儲存；儲存後才能啟動" : "使用已儲存的設定啟動"; color: "#94a3b8" }
    }
    Label { text: editor.validationError; visible: text.length > 0; color: "#fca5a5"; wrapMode: Text.Wrap; Layout.fillWidth: true }
}
