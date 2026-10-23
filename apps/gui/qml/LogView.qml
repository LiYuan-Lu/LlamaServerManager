import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

// Console of the selected model: live stdout/stderr with search, level filters
// (inferred from the text), pause, copy, export, and clear. Clearing never deletes log files.
ColumnLayout {
    id: view
    readonly property var log: agent.logModel
    property string notice: ""
    property bool noticeIsError: false
    spacing: 6

    function levelColor(level, channel) {
        if (level === "error") return "#fca5a5"
        if (level === "warning") return "#fcd34d"
        return channel === "agent" ? "#93c5fd" : "#e5e7eb"
    }
    function copy(text) {
        clipboard.text = text
        clipboard.selectAll()
        clipboard.copy()
        clipboard.text = ""
    }
    function showNotice(text, error) {
        notice = text; noticeIsError = error; noticeTimer.restart()
    }

    // Off-screen helper: Qt Quick has no clipboard API, but TextEdit can copy.
    TextEdit { id: clipboard; visible: false }
    Timer { id: noticeTimer; interval: 4000; onTriggered: view.notice = "" }
    FileDialog {
        id: exportDialog
        title: qsTr("Export log")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "log"
        nameFilters: [qsTr("Log files (*.log *.txt)"), qsTr("All files (*)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            const error = view.log.exportTo(selectedFile, filtered.checked)
            if (error) view.showNotice(error, true)
            else view.showNotice(qsTr("Exported to %1").arg(agent.localPath(selectedFile)), false)
        }
    }

    RowLayout {
        Layout.fillWidth: true; spacing: 6
        TextField {
            id: search
            placeholderText: qsTr("Search…")
            Layout.preferredWidth: 220
            onTextChanged: searchDelay.restart()
            Timer { id: searchDelay; interval: 250; onTriggered: view.log.filterText = search.text }
        }
        Button {
            checkable: true; checked: view.log.showErrors
            onToggled: view.log.showErrors = checked
            text: qsTr("Errors %1").arg(view.log.errorCount)
            palette.buttonText: "#fca5a5"; opacity: checked ? 1 : 0.5
        }
        Button {
            checkable: true; checked: view.log.showWarnings
            onToggled: view.log.showWarnings = checked
            text: qsTr("Warnings %1").arg(view.log.warningCount)
            palette.buttonText: "#fcd34d"; opacity: checked ? 1 : 0.5
        }
        Button {
            checkable: true; checked: view.log.showInfo
            onToggled: view.log.showInfo = checked
            text: qsTr("Info %1").arg(view.log.infoCount)
            opacity: checked ? 1 : 0.5
        }
        Item { Layout.fillWidth: true }
        Button {
            text: view.log.paused ? (view.log.heldCount > 0 ? qsTr("▶ Resume (%1 new)").arg(view.log.heldCount) : qsTr("▶ Resume")) : qsTr("❚❚ Pause")
            onClicked: view.log.paused = !view.log.paused
        }
        CheckBox { id: follow; text: qsTr("Auto-scroll"); checked: true }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 6
        Label {
            text: view.log.visibleCount === view.log.totalCount
                ? qsTr("%1 lines").arg(view.log.totalCount)
                : qsTr("%1 of %2 lines match").arg(view.log.visibleCount).arg(view.log.totalCount)
            color: "#94a3b8"
        }
        Label {
            text: view.notice; visible: text.length > 0
            color: view.noticeIsError ? "#fca5a5" : "#6ee7b7"
            elide: Text.ElideMiddle; Layout.fillWidth: true
        }
        Item { Layout.fillWidth: true; visible: view.notice.length === 0 }
        CheckBox {
            id: filtered; text: qsTr("Only matching lines")
            checked: true
            ToolTip.visible: hovered; ToolTip.text: qsTr("Copy and export only the lines that match the search and level filters")
        }
        Button {
            text: qsTr("Copy")
            enabled: view.log.totalCount > 0
            onClicked: { view.copy(view.log.text(filtered.checked)); view.showNotice(qsTr("Copied to the clipboard"), false) }
        }
        Button {
            text: qsTr("Export…")
            enabled: view.log.totalCount > 0
            onClicked: {
                const stamp = new Date().toISOString().replace(/[-:]/g, "").replace("T", "-").slice(0, 15)
                const name = ((agent.instances[agent.selectedId] || {}).name || "model").replace(/[\\/:*?"<>|]/g, "_")
                exportDialog.selectedFile = exportDialog.currentFolder + "/" + name + "-" + stamp + ".log"
                exportDialog.open()
            }
        }
        Button {
            text: qsTr("Clear view")
            ToolTip.visible: hovered; ToolTip.text: qsTr("Clears this view only; log files on disk are kept")
            onClicked: agent.clearLogs()
        }
    }
    Rectangle {
        Layout.fillWidth: true; Layout.fillHeight: true
        color: "#0b1220"; radius: 6; border.color: "#334155"
        ListView {
            id: list
            anchors.fill: parent; anchors.margins: 6
            clip: true
            model: view.log
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            onCountChanged: if (follow.checked && !view.log.paused) Qt.callLater(list.positionViewAtEnd)
            delegate: RowLayout {
                width: list.width - 14
                spacing: 8
                Label {
                    text: model.time; color: "#64748b"
                    font.family: "Consolas"; font.pixelSize: 12
                    Layout.alignment: Qt.AlignTop
                }
                Rectangle {
                    Layout.preferredWidth: 3; Layout.fillHeight: true
                    color: model.level === "error" ? "#ef4444" : model.level === "warning" ? "#f59e0b" : "transparent"
                }
                TextEdit {
                    text: model.text
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                    color: view.levelColor(model.level, model.channel)
                    selectionColor: "#2563eb"
                    font.family: "Consolas"; font.pixelSize: 12
                    Layout.fillWidth: true
                }
            }
        }
        Label {
            anchors.centerIn: parent
            visible: list.count === 0
            text: view.log.totalCount > 0 ? qsTr("No lines match the current filters") : qsTr("No output yet")
            color: "#64748b"
        }
    }
}
