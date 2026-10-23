import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

// Detail view for one model; profileId === "" means a new, not yet saved model.
Item {
    id: page
    property string profileId: ""
    property int suggestedPort: 8080
    // Settings to prefill a new model with (Duplicate); ignored for saved models.
    property var template: null
    readonly property bool isNew: profileId === ""
    readonly property var info: agent.instances[profileId] || ({})
    readonly property string serverState: info.state || "STOPPED"
    readonly property bool idle: serverState === "STOPPED" || serverState === "FAILED"
    // Generation speed from GET /slots, sampled by the Agent once per second.
    readonly property var throughput: info.throughput || ({})
    readonly property var speedSeries: agent.throughputHistory[profileId] || ({ times: [], values: [] })
    readonly property string speedText: {
        if (!info.pid) return "—"
        if (throughput.available !== true) return (throughput.reason || "").indexOf("disabled") >= 0 ? qsTr("off") : "—"
        if (throughput.activeSlots > 0) return qsTr("%1 tok/s").arg(Number(throughput.tokensPerSecond).toFixed(1))
        return qsTr("idle")
    }
    readonly property string speedDetail: {
        if (throughput.available !== true) {
            if (!info.pid) return qsTr("Not running")
            return (throughput.reason || "").indexOf("disabled") >= 0
                ? qsTr("The /slots endpoint is disabled (--no-slots), so the speed is not available")
                : qsTr("Waiting for /slots")
        }
        const parts = [qsTr("Busy slots %1/%2").arg(throughput.activeSlots).arg(throughput.slots)]
        if (throughput.lastTokensPerSecond > 0) parts.unshift(qsTr("Last generation %1 tok/s").arg(Number(throughput.lastTokensPerSecond).toFixed(1)))
        return parts.join("  ·  ")
    }
    signal back()
    signal duplicate(var profile)
    // Leaving with unsaved edits asks first: save, discard, or keep editing.
    property var pendingLeave: null
    property bool leaveAfterSave: false
    function requestLeave(action) {
        if (!editor.dirty) {
            action()
            return
        }
        pendingLeave = action
        confirmLeave.open()
    }
    function saveAndLeave() {
        leaveAfterSave = true
        editor.save()
        if (!editor.savePending) { // invalid input: stay, the error is shown above the editor
            leaveAfterSave = false
            pendingLeave = null
        }
    }
    function finishLeave() {
        const action = pendingLeave
        pendingLeave = null
        if (action) Qt.callLater(action)
    }
    function healthText(health) {
        if (!health) return "—"
        if (health === "Healthy") return qsTr("Healthy")
        if (health === "Disabled") return qsTr("Health checks off")
        if (health === "Not running") return qsTr("Not running")
        if (health.indexOf("Loading model") === 0) return qsTr("Loading model (HTTP 503)")
        if (health.indexOf("Waiting for /health") === 0) return qsTr("Waiting for /health")
        return health
    }

    Component.onCompleted: agent.selectedId = profileId
    onProfileIdChanged: agent.selectedId = profileId
    Connections {
        target: agent
        function onProfileCreated(id) { if (page.isNew) page.profileId = id }
        function onInstanceIdsChanged() { if (!page.isNew && agent.instanceIds.indexOf(page.profileId) < 0) page.back() }
    }
    Connections {
        target: editor
        // savePending clears before the editor reloads the saved profile, so check afterwards.
        function onSavePendingChanged() {
            if (editor.savePending || !page.leaveAfterSave) return
            page.leaveAfterSave = false
            Qt.callLater(() => { if (!editor.dirty) page.finishLeave(); else page.pendingLeave = null })
        }
    }

    Dialog {
        id: confirmLeave
        anchors.centerIn: parent
        modal: true
        title: qsTr("Unsaved changes")
        width: 460
        Label {
            width: confirmLeave.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("This model has unsaved changes. Save them before leaving?")
        }
        footer: DialogButtonBox {
            Button {
                text: page.isNew ? qsTr("Add and leave") : qsTr("Save and leave")
                enabled: editor.editable && !editor.savePending
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: { confirmLeave.close(); page.saveAndLeave() }
            }
            Button {
                text: qsTr("Discard changes")
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: { confirmLeave.close(); page.finishLeave() }
            }
            Button {
                text: qsTr("Keep editing")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onRejected: page.pendingLeave = null
    }

    // Shows an opencode provider config built from the running server's /v1/models and /props.
    Dialog {
        id: opencodeDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("opencode config")
        width: Math.min(page.width - 40, 680)
        property bool loading: false
        property bool copied: false
        property string config: ""
        property string error: ""
        function request() {
            config = ""; error = ""; copied = false; loading = true
            open()
            agent.requestOpencodeConfig(page.profileId)
        }
        Connections {
            target: agent
            function onOpencodeConfigReady(id, config, error) {
                if (id !== page.profileId) return
                opencodeDialog.loading = false
                opencodeDialog.config = config
                opencodeDialog.error = error
            }
        }
        ColumnLayout {
            width: opencodeDialog.availableWidth; spacing: 8
            Label {
                text: qsTr("Paste into opencode.json. If the file already has a \"provider\" section, add this provider to it.")
                color: "#94a3b8"; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Label { visible: opencodeDialog.loading; text: qsTr("Reading /v1/models and /props…") }
            Label {
                visible: opencodeDialog.error.length > 0
                text: opencodeDialog.error; color: "#fca5a5"; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            ScrollView {
                visible: opencodeDialog.config.length > 0
                Layout.fillWidth: true; Layout.preferredHeight: 340
                TextArea {
                    id: opencodeText
                    text: opencodeDialog.config
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.NoWrap
                    font.family: "Consolas"; font.pixelSize: 12
                }
            }
        }
        footer: DialogButtonBox {
            Button {
                text: opencodeDialog.copied ? qsTr("Copied") : qsTr("Copy")
                enabled: opencodeDialog.config.length > 0
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: {
                    opencodeText.selectAll(); opencodeText.copy(); opencodeText.deselect()
                    opencodeDialog.copied = true
                }
            }
            Button {
                text: qsTr("Close")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
    }

    Dialog {
        id: confirmDelete
        anchors.centerIn: parent
        modal: true
        title: qsTr("Delete model")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: qsTr("Delete \"%1\"? Its profile and logs will be removed. The model file itself is not touched.").arg(page.info.name || ""); wrapMode: Text.Wrap; width: 360 }
        onAccepted: agent.deleteProfile(page.profileId)
    }

    ColumnLayout {
        anchors.fill: parent; spacing: 12
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            Button { text: qsTr("← All models"); flat: true; onClicked: page.requestLeave(() => page.back()) }
            Label {
                text: page.isNew ? (page.template ? qsTr("New model (copy)") : qsTr("New model")) : (page.info.name || "")
                font.pixelSize: 20; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true
            }
            StateBadge { visible: !page.isNew; serverState: page.serverState }
            Button {
                visible: !page.isNew
                text: qsTr("opencode config")
                enabled: page.serverState === "RUNNING"
                ToolTip.visible: hovered; ToolTip.text: qsTr("Read the model ID from /v1/models and show a config for opencode")
                onClicked: opencodeDialog.request()
            }
            Button {
                visible: !page.isNew
                text: qsTr("Duplicate")
                enabled: page.info.profile !== undefined
                ToolTip.visible: hovered; ToolTip.text: qsTr("Create a new model with a copy of these settings")
                onClicked: page.requestLeave(() => page.duplicate(page.info.profile))
            }
            Button {
                visible: !page.isNew
                text: qsTr("Delete")
                enabled: agent.connected && page.idle && page.info.busy !== true
                onClicked: confirmDelete.open()
            }
        }
        Frame {
            visible: !page.isNew
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent; spacing: 16
                Label { text: qsTr("PID  %1").arg(page.info.pid || "—") }
                Label { text: qsTr("Port  %1").arg((page.info.profile || {}).port || "—") }
                Label { text: qsTr("Uptime  %1").arg(page.info.pid ? Theme.duration(page.info.uptimeSeconds) : "—") }
                Label { text: qsTr("Health  %1").arg(page.healthText(page.info.health)); elide: Text.ElideRight; Layout.fillWidth: true }
                Label {
                    text: qsTr("Speed  %1").arg(page.speedText)
                    ToolTip.visible: speedHover.hovered; ToolTip.text: page.speedDetail
                    HoverHandler { id: speedHover }
                }
                Label {
                    text: qsTr("Restarts  %1/%2").arg(page.info.restartsInWindow || 0).arg(page.info.maxRestarts === undefined ? "—" : page.info.maxRestarts)
                    ToolTip.visible: restartsHover.hovered; ToolTip.text: qsTr("Automatic restarts within the restart window")
                    HoverHandler { id: restartsHover }
                }
                ServerControls {
                    fillToggle: false
                    profileId: page.profileId; info: page.info; startBlocked: editor.dirty
                }
            }
        }
        UsageLabel { profileId: page.profileId; Layout.fillWidth: true }
        Label {
            visible: !page.isNew
            text: qsTr("With health checks on, the model stays STARTING until GET /health returns 200, then becomes RUNNING. Failed checks, crashes, and startup failures trigger automatic restarts with backoff; Stop always cancels them.")
            color: "#94a3b8"; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        Label {
            text: page.info.error || ""; visible: !page.isNew && text.length > 0
            color: "#fca5a5"; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        // Always visible, above the scrolling editor, so Save cannot be missed.
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Button {
                text: page.isNew ? qsTr("Add model") : qsTr("Save profile")
                highlighted: editor.dirty
                enabled: editor.editable && !editor.savePending
                onClicked: editor.save()
            }
            Button { text: qsTr("Revert changes"); visible: !page.isNew; enabled: editor.editable && editor.dirty; onClicked: editor.load() }
            Label {
                text: page.isNew ? qsTr("Fill in the paths, then add the model to the list")
                    : editor.dirty ? qsTr("Unsaved changes; save before starting") : qsTr("Starts with the saved profile")
                color: editor.dirty && !page.isNew ? "#fbbf24" : "#94a3b8"
                elide: Text.ElideRight; Layout.fillWidth: true
            }
        }
        Label { text: editor.validationError; visible: text.length > 0; color: "#fca5a5"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        SplitView {
            orientation: Qt.Vertical; Layout.fillWidth: true; Layout.fillHeight: true
            ScrollView {
                // The settings get most of the height; the console below can be dragged larger.
                SplitView.preferredHeight: page.isNew ? 10000 : Math.max(300, page.height * 0.55); SplitView.minimumHeight: 120
                // Keep the Browse buttons and fields clear of the scroll bar.
                rightPadding: ScrollBar.vertical.width + 8
                contentWidth: availableWidth
                ProfileEditor {
                    id: editor; width: parent.width
                    profileId: page.profileId; suggestedPort: page.suggestedPort; template: page.template
                    editable: agent.connected && page.idle && page.info.busy !== true
                }
            }
            ColumnLayout {
                visible: !page.isNew; SplitView.fillHeight: true; SplitView.minimumHeight: 220; spacing: 6
                TabBar {
                    id: tabs
                    TabButton { text: qsTr("Console"); width: implicitWidth + 24 }
                    TabButton { text: qsTr("Restart history (%1)").arg(agent.restartHistory.length); width: implicitWidth + 24 }
                    TabButton { text: qsTr("Speed"); width: implicitWidth + 24 }
                }
                StackLayout {
                    currentIndex: tabs.currentIndex
                    Layout.fillWidth: true; Layout.fillHeight: true
                    LogView {}
                    RestartHistoryView {}
                    MetricTile {
                        title: qsTr("Generation speed (tokens per second, all slots)")
                        value: page.speedText
                        detail: page.speedDetail
                        accent: "#a78bfa"
                        times: page.speedSeries.times
                        values: page.speedSeries.values
                        maxValue: Math.max(10, Math.ceil(Math.max.apply(null, page.speedSeries.values.concat([0])) * 1.2 / 10) * 10)
                        emptyText: page.speedSeries.times.length > 0 ? "" : page.info.pid ? qsTr("No samples yet. The speed appears while the model runs.") : qsTr("Start the model to see its generation speed")
                    }
                }
            }
        }
    }
}
