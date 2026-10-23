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
    // true: the toggle stretches to fill the row (model cards).
    // false: the toggle keeps a fixed width sized to its longest label (model page).
    property bool fillToggle: true
    spacing: 8

    Button {
        id: toggle
        Layout.fillWidth: controls.fillToggle
        // Same width for every state so the button does not jump when it toggles.
        Layout.preferredWidth: Math.max(startMetrics.advanceWidth, stopMetrics.advanceWidth, stoppingMetrics.advanceWidth)
            + leftPadding + rightPadding + 8
        text: controls.idle ? qsTr("▶ Start")
            : controls.serverState === "STOPPING" ? qsTr("Stopping…") : qsTr("■ Stop")
        enabled: agent.connected && !controls.busy && controls.serverState !== "STOPPING"
            && (!controls.idle || !controls.startBlocked)
        font.bold: true
        leftPadding: 14; rightPadding: 14; topPadding: 6; bottomPadding: 6
        readonly property color baseColor: controls.idle ? "#047857" : "#b91c1c"

        // Rounded solid button with hover/pressed shades so it reads as clickable.
        background: Rectangle {
            implicitHeight: 30
            radius: 6
            color: toggle.down ? Qt.darker(toggle.baseColor, 1.25)
                : toggle.hovered ? Qt.lighter(toggle.baseColor, 1.15) : toggle.baseColor
            border.width: toggle.visualFocus ? 2 : 0
            border.color: "#ffffff"
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        contentItem: Text {
            text: toggle.text
            font: toggle.font
            color: "#ffffff"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        HoverHandler { cursorShape: toggle.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
        opacity: enabled ? 1 : 0.45
        onClicked: controls.idle ? agent.start(controls.profileId) : agent.stop(controls.profileId)

        TextMetrics { id: startMetrics; font: toggle.font; text: qsTr("▶ Start") }
        TextMetrics { id: stopMetrics; font: toggle.font; text: qsTr("■ Stop") }
        TextMetrics { id: stoppingMetrics; font: toggle.font; text: qsTr("Stopping…") }
    }
    Button {
        id: restart
        text: qsTr("⟳ Restart")
        leftPadding: 14; rightPadding: 14; topPadding: 6; bottomPadding: 6
        readonly property color baseColor: "#334155"

        // Same rounded shape as the toggle, in neutral slate as the secondary action.
        background: Rectangle {
            implicitHeight: 30
            radius: 6
            color: restart.down ? Qt.darker(restart.baseColor, 1.25)
                : restart.hovered ? Qt.lighter(restart.baseColor, 1.25) : restart.baseColor
            border.width: restart.visualFocus ? 2 : 1
            border.color: restart.visualFocus ? "#ffffff" : "#475569"
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        contentItem: Text {
            text: restart.text
            font: restart.font
            color: "#f8fafc"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        HoverHandler { cursorShape: restart.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
        opacity: enabled ? 1 : 0.45
        enabled: agent.connected && !controls.busy && ["RUNNING", "UNHEALTHY", "BACKOFF", "RESTARTING"].indexOf(controls.serverState) >= 0
        onClicked: agent.restart(controls.profileId)
    }
}
