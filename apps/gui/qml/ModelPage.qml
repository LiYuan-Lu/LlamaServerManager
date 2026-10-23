import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

// Detail view for one model; profileId === "" means a new, not yet saved model.
Item {
    id: page
    property string profileId: ""
    property int suggestedPort: 8080
    readonly property bool isNew: profileId === ""
    readonly property var info: agent.instances[profileId] || ({})
    readonly property string serverState: info.state || "STOPPED"
    readonly property bool idle: serverState === "STOPPED" || serverState === "FAILED"
    signal back()

    Component.onCompleted: agent.selectedId = profileId
    onProfileIdChanged: agent.selectedId = profileId
    Connections {
        target: agent
        function onProfileCreated(id) { if (page.isNew) page.profileId = id }
        function onInstanceIdsChanged() { if (!page.isNew && agent.instanceIds.indexOf(page.profileId) < 0) page.back() }
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
            Button { text: qsTr("← All models"); flat: true; onClicked: page.back() }
            Label {
                text: page.isNew ? qsTr("New model") : (page.info.name || "")
                font.pixelSize: 20; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true
            }
            StateBadge { visible: !page.isNew; serverState: page.serverState }
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
                Label { text: qsTr("Uptime  %1").arg(page.serverState === "RUNNING" ? Theme.duration(page.info.uptimeSeconds) : "—"); Layout.fillWidth: true }
                ServerControls {
                    Layout.preferredWidth: 260
                    profileId: page.profileId; info: page.info; startBlocked: editor.dirty
                }
            }
        }
        Label {
            visible: !page.isNew
            text: qsTr("RUNNING means the process has started; check the logs for model loading progress. HTTP health checks are not performed yet.")
            color: "#94a3b8"; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        Label {
            text: page.info.error || ""; visible: !page.isNew && text.length > 0
            color: "#fca5a5"; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        SplitView {
            orientation: Qt.Vertical; Layout.fillWidth: true; Layout.fillHeight: true
            ScrollView {
                SplitView.preferredHeight: page.isNew ? 10000 : 360; SplitView.minimumHeight: 120
                contentWidth: availableWidth
                ProfileEditor {
                    id: editor; width: parent.width
                    profileId: page.profileId; suggestedPort: page.suggestedPort
                    editable: agent.connected && page.idle && page.info.busy !== true
                }
            }
            LogView { visible: !page.isNew; SplitView.fillHeight: true; SplitView.minimumHeight: 150 }
        }
    }
}
