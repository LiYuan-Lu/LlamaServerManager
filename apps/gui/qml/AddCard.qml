import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: card
    signal clicked()
    implicitHeight: 196
    radius: 10
    color: hover.hovered ? "#1e293b" : "transparent"
    border.color: hover.hovered ? "#60a5fa" : "#475569"
    border.width: 2
    opacity: agent.connected ? 1 : 0.5

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor; enabled: agent.connected }
    TapHandler { enabled: agent.connected; onTapped: card.clicked() }

    ColumnLayout {
        anchors.centerIn: parent; spacing: 4
        Label {
            text: "+"; font.pixelSize: 48; font.weight: Font.Light
            color: hover.hovered ? "#93c5fd" : "#94a3b8"
            Layout.alignment: Qt.AlignHCenter
        }
        Label { text: qsTr("Add model"); color: "#94a3b8"; Layout.alignment: Qt.AlignHCenter }
    }
}
