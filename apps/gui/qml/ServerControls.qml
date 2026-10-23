import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One toggle button (Start ⇄ Stop) plus Restart for a single model.
RowLayout {
    id: controls
    property string profileId: ""
    property var info: ({})
    // Extra reason to block starting, e.g. unsaved profile edits.
    property bool startBlocked: false
    readonly property string serverState: info.state || "STOPPED"
    readonly property bool idle: serverState === "STOPPED" || serverState === "FAILED"
    readonly property bool busy: info.busy === true
    spacing: 8

    Button {
        id: toggle
        Layout.fillWidth: true
        text: controls.idle ? qsTr("▶ Start")
            : controls.serverState === "STOPPING" ? qsTr("Stopping…") : qsTr("■ Stop")
        enabled: agent.connected && !controls.busy && controls.serverState !== "STOPPING"
            && (!controls.idle || !controls.startBlocked)
        palette.button: controls.idle ? "#047857" : "#b91c1c"
        palette.buttonText: "#ffffff"
        opacity: enabled ? 1 : 0.45
        onClicked: controls.idle ? agent.start(controls.profileId) : agent.stop(controls.profileId)
    }
    Button {
        text: qsTr("⟳ Restart")
        opacity: enabled ? 1 : 0.45
        enabled: agent.connected && controls.serverState === "RUNNING" && !controls.busy
        onClicked: agent.restart(controls.profileId)
    }
}
