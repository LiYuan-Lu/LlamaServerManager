import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One dashboard tile: current value, a detail line, and a 5-minute chart.
Rectangle {
    id: tile
    property string title: ""
    property string value: "—"
    property string detail: ""
    property color accent: "#38bdf8"
    property alias times: chart.times
    property alias values: chart.values
    property alias maxValue: chart.maxValue
    property alias gapMs: chart.gapMs
    property alias emptyText: chart.emptyText
    implicitHeight: 156
    radius: 10
    color: "#1f2937"
    border.color: "#334155"

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 2
        RowLayout {
            Layout.fillWidth: true
            Rectangle { width: 8; height: 8; radius: 4; color: tile.accent }
            Label { text: tile.title; color: "#94a3b8"; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
        }
        Label { text: tile.value; font.pixelSize: 22; font.bold: true; color: "#f8fafc" }
        Label {
            text: tile.detail; color: "#94a3b8"; font.pixelSize: 11
            elide: Text.ElideRight; Layout.fillWidth: true
            ToolTip.visible: detailHover.hovered && truncated; ToolTip.text: text
            HoverHandler { id: detailHover }
        }
        LineChart {
            id: chart
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.topMargin: 4
            color: tile.accent
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("5 min ago"); color: "#64748b"; font.pixelSize: 10; Layout.fillWidth: true }
            Label { text: qsTr("now"); color: "#64748b"; font.pixelSize: 10 }
        }
    }
}
