import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

Rectangle {
    id: card
    property string profileId: ""
    readonly property var info: agent.instances[profileId] || ({})
    readonly property var profile: info.profile || ({})
    readonly property string serverState: info.state || "STOPPED"
    signal open()
    implicitHeight: 176
    radius: 10
    color: hover.hovered ? "#243244" : "#1f2937"
    border.color: serverState === "RUNNING" ? "#065f46" : serverState === "FAILED" ? "#7f1d1d" : "#334155"
    border.width: 1

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: card.open() }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 14; spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: card.info.name || qsTr("Untitled model")
                font.pixelSize: 17; font.bold: true
                elide: Text.ElideRight; Layout.fillWidth: true
            }
            StateBadge { serverState: card.serverState }
        }
        Label {
            text: Theme.fileName(card.profile.model) || qsTr("No model file selected")
            color: "#94a3b8"; elide: Text.ElideMiddle; Layout.fillWidth: true
        }
        Label {
            text: ("%1:%2").arg(card.profile.host || "127.0.0.1").arg(card.profile.port || "—")
                + (card.info.pid ? qsTr("  ·  PID %1").arg(card.info.pid) : "")
                + (card.serverState === "RUNNING" ? qsTr("  ·  up %1").arg(Theme.duration(card.info.uptimeSeconds)) : "")
            color: "#cbd5e1"; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
        }
        Label {
            text: card.serverState === "FAILED" ? (card.info.error || "") : ""
            visible: text.length > 0
            color: "#fca5a5"; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
        }
        Item { Layout.fillHeight: true }
        ServerControls { Layout.fillWidth: true; profileId: card.profileId; info: card.info }
    }
}
