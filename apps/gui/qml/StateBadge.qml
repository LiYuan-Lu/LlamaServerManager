import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

Rectangle {
    property string serverState: "STOPPED"
    implicitWidth: badgeText.implicitWidth + 18
    implicitHeight: badgeText.implicitHeight + 6
    radius: height / 2
    color: Qt.rgba(0, 0, 0, 0.25)
    border.color: Theme.stateColor(serverState)
    Label {
        id: badgeText
        anchors.centerIn: parent
        text: parent.serverState
        color: Theme.stateColor(parent.serverState)
        font.pixelSize: 11; font.bold: true
    }
}
