import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    spacing: 6
    RowLayout {
        Label { text: "Console · stdout / stderr"; font.bold: true; Layout.fillWidth: true }
        CheckBox { id: follow; text: "自動捲動"; checked: true }
        Button { text: "複製全部"; onClicked: { consoleText.selectAll(); consoleText.copy(); consoleText.deselect() } }
        Button { text: "清除畫面"; onClicked: agent.clearLogs() }
    }
    ScrollView {
        id: scroll; Layout.fillWidth: true; Layout.fillHeight: true
        TextArea {
            id: consoleText; text: agent.logText; readOnly: true; selectByMouse: true
            font.family: "Consolas"; font.pixelSize: 12; wrapMode: TextEdit.Wrap
            onTextChanged: if (follow.checked) cursorPosition = length
        }
    }
}
