// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Controls
import org.kde.kirigami as Kirigami
import org.kde.milou as Milou

ListView {
    id: results
    objectName: "searchResults"
    property var backend: null
    property var inputField: null
    readonly property var colors: backend ? backend.theme
        : ({ surface: "#1e1e1e", text: "#eaeaea", muted: "#999999", accent: "#4bc8ff" })
    property alias queryString: provider.queryString
    property alias querying: provider.querying
    property alias limit: provider.limit
    property string pendingQuery: ""
    property bool waitingForResults: false
    clip: true
    spacing: 4
    keyNavigationWraps: true
    highlightMoveDuration: 0
    model: Milou.ResultsModel {
        id: provider
        queryString: results.inputField ? results.inputField.text.trim() : ""
        limit: 20
        onQueryStringChanged: {
            results.pendingQuery = ""
            results.waitingForResults = true
            results.currentIndex = 0
        }
        onQueryStringChangeRequested: (text, cursorPosition) => {
            if (results.inputField) {
                results.inputField.text = text
                results.inputField.cursorPosition = cursorPosition
            }
        }
        onQueryingChanged: {
            if (!querying) results.waitingForResults = false
            results.tryPendingActivation()
        }
    }
    Connections {
        target: results.model
        function onDataChanged() { results.resultsArrived() }
        function onRowsInserted() { results.resultsArrived() }
        function onRowsRemoved() { results.resultsArrived() }
        function onModelReset() { results.resultsArrived() }
    }
    function resultsArrived() {
        waitingForResults = false
        tryPendingActivation()
    }
    function activate(row, action = -1) {
        if (waitingForResults) return
        pendingQuery = ""
        if (backend) backend.runSearchResult(provider, row, action)
    }
    function runCurrentIndex(event) {
        if (count === 0 || waitingForResults) {
            if (querying || waitingForResults) pendingQuery = queryString
            return
        }
        if (event && event.modifiers === Qt.ShiftModifier) {
            const actions = provider.data(provider.index(currentIndex, 0), Milou.ResultsModel.ActionsRole)
            if (actions && actions.length > 0) { activate(currentIndex, 0); return }
        }
        activate(currentIndex)
    }
    function tryPendingActivation() {
        if (!pendingQuery) return
        Qt.callLater(() => {
            if (pendingQuery && pendingQuery === queryString && count > 0 && !waitingForResults) activate(0)
            else if (!querying && !waitingForResults) pendingQuery = ""
        })
    }
    function showFileMenu(row, item, x, y) {
        const path = backend ? backend.searchResultPath(provider, row) : ""
        const itemData = backend ? backend.searchResultItem(provider, row) : ({})
        if (!path && !itemData.key) return
        currentIndex = row
        fileMenu.targetPath = path
        fileMenu.pinItem = itemData
        fileMenu.locationLabel = qsTr("File location")
        const point = item.mapToItem(results, x, y)
        fileMenu.popup(results, point.x, point.y)
    }
    onCountChanged: tryPendingActivation()
    Keys.onReturnPressed: event => runCurrentIndex(event)
    Keys.onEnterPressed: event => runCurrentIndex(event)
    Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
    section.property: "category"
    section.delegate: Text {
        required property string section
        text: section
        textFormat: Text.PlainText
        color: results.colors.muted
        font.pixelSize: 11
        topPadding: 7
        bottomPadding: 4
    }
    delegate: Controls.ItemDelegate {
        id: row
        required property int index
        required property var model
        required property var actions
        objectName: "searchResult_" + index
        width: results.width
        height: 58
        enabled: model.enabled === true && !results.waitingForResults
        hoverEnabled: true
        Accessible.name: model.display || ""
        Accessible.description: model.subtext || ""
        onClicked: { results.currentIndex = index; results.activate(index) }
        onHoveredChanged: { if (hovered) results.currentIndex = index }
        background: Rectangle {
            radius: 7
            color: row.down ? Qt.alpha(results.colors.accent, 0.22)
                : row.hovered || row.ListView.isCurrentItem ? Qt.alpha(results.colors.text, 0.08) : "transparent"
            border.width: row.activeFocus ? 1 : 0
            border.color: results.colors.accent
        }
        contentItem: RowLayout {
            spacing: 10
            Kirigami.Icon { source: row.model.decoration; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: row.model.display || ""
                    textFormat: Text.PlainText
                    color: results.colors.text
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: row.model.subtext || ""
                    textFormat: Text.PlainText
                    color: results.colors.muted
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                }
            }
            Row {
                visible: row.ListView.isCurrentItem
                Repeater {
                    model: row.actions
                    Controls.ToolButton {
                        id: actionButton
                        required property var modelData
                        required property int index
                        width: 30
                        height: 30
                        Accessible.name: modelData.text
                        enabled: modelData.enabled !== false
                        onClicked: results.activate(row.index, index)
                        contentItem: Kirigami.Icon { source: actionButton.modelData.iconSource || ""; color: results.colors.text }
                        background: Rectangle { radius: 5; color: actionButton.hovered ? Qt.alpha(results.colors.accent, 0.15) : "transparent" }
                    }
                }
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: point => results.showFileMenu(row.index, row, point.position.x, point.position.y)
        }
        Keys.onMenuPressed: results.showFileMenu(index, row, 16, height / 2)
    }
    RevealMenu {
        id: fileMenu
        objectName: "searchFileContextMenu"
        actionObjectName: "revealSearchFile"
        paletteColors: results.colors
        backend: results.backend
        onRevealRequested: results.backend.revealPath(targetPath)
    }
}
