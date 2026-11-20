// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Controls
import org.kde.kirigami as Kirigami

FocusScope {
    id: view
    required property var backend
    required property var paletteColors
    property bool listMode: false
    property string contextDesktopId: ""

    function showApplicationMenu(desktopId, item, x, y) {
        contextDesktopId = desktopId
        appMenu.pinItem = backend.applicationItem(desktopId)
        appMenu.targetPath = ""
        appMenu.locationLabel = qsTr("Finding app files…")
        appMenu.busy = true
        const point = item.mapToItem(view, x, y)
        appMenu.popup(view, point.x, point.y)
        backend.requestApplicationLocation(desktopId)
    }
    Connections {
        target: view.backend
        function onApplicationLocationReady(desktopId, path, label) {
            if (desktopId !== view.contextDesktopId) return
            appMenu.targetPath = path
            appMenu.locationLabel = label
            appMenu.busy = false
        }
    }

    function focusSearch() { filter.forceActiveFocus() }
    function begin() {
        filter.clear()
        backend.filteredApplications.category = ""
        backend.applications.refresh()
        apps.positionViewAtBeginning()
        filter.forceActiveFocus()
    }
    function escapeSearch() {
        if (filter.text.length > 0) { filter.clear(); filter.forceActiveFocus() }
        else backend.dismiss()
    }
    function openCurrent() {
        const desktopId = backend.filteredApplications.desktopIdAt(apps.currentIndex)
        if (desktopId) backend.launchApplication(desktopId)
    }
    function changeMode(asList) {
        listMode = asList
        Qt.callLater(() => {
            apps.positionViewAtIndex(apps.currentIndex, GridView.Contain)
            apps.forceActiveFocus()
        })
    }
    Keys.onEscapePressed: escapeSearch()
    Connections {
        target: view.backend.filteredApplications
        function onQueryChanged() {
            apps.currentIndex = view.backend.filteredApplications.count > 0 ? 0 : -1
            apps.positionViewAtBeginning()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Controls.TextField {
                id: filter
                objectName: "applicationsSearchField"
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                leftPadding: 40
                rightPadding: 36
                placeholderText: qsTr("Filter applications…")
                placeholderTextColor: view.paletteColors.muted
                color: view.paletteColors.text
                selectionColor: view.paletteColors.accent
                selectedTextColor: view.paletteColors.surface
                selectByMouse: true
                font.pixelSize: 14
                onTextChanged: view.backend.filteredApplications.query = text
                background: Rectangle {
                    radius: 7
                    color: view.paletteColors.surface
                    border.width: 1
                    border.color: filter.activeFocus ? Qt.alpha(view.paletteColors.accent, 0.6)
                                                    : Qt.alpha(view.paletteColors.text, 0.12)
                }
                Kirigami.Icon {
                    source: "system-search"
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    color: view.paletteColors.muted
                }
                Controls.ToolButton {
                    visible: filter.text.length > 0
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32
                    Accessible.name: qsTr("Clear applications filter")
                    onClicked: { filter.clear(); filter.forceActiveFocus() }
                    contentItem: Text {
                        text: "×"
                        color: view.paletteColors.muted
                        font.pixelSize: 18
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Item {}
                }
                Keys.onEscapePressed: view.escapeSearch()
                Keys.onDownPressed: {
                    if (apps.currentIndex < 0 && apps.count > 0) apps.currentIndex = 0
                    apps.positionViewAtIndex(apps.currentIndex, GridView.Contain)
                    apps.forceActiveFocus()
                }
                Keys.onReturnPressed: view.openCurrent()
                Keys.onEnterPressed: view.openCurrent()
            }
            Repeater {
                model: [ { label: qsTr("Gallery view"), icon: "view-grid", asList: false },
                         { label: qsTr("List view"), icon: "view-list-details", asList: true } ]
                Controls.ToolButton {
                    id: modeButton
                    required property var modelData
                    objectName: modelData.asList ? "applicationsListMode" : "applicationsGalleryMode"
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    checkable: true
                    autoExclusive: true
                    checked: view.listMode === modelData.asList
                    Accessible.name: modelData.label
                    onClicked: view.changeMode(modelData.asList)
                    contentItem: Kirigami.Icon { source: modeButton.modelData.icon; color: view.paletteColors.text }
                    background: Rectangle {
                        radius: 7
                        color: modeButton.checked ? Qt.alpha(view.paletteColors.accent, 0.18)
                                                  : modeButton.hovered ? Qt.alpha(view.paletteColors.text, 0.08) : "transparent"
                        border.width: modeButton.checked || modeButton.activeFocus ? 1 : 0
                        border.color: Qt.alpha(view.paletteColors.accent, 0.65)
                    }
                    Controls.ToolTip.visible: hovered
                    Controls.ToolTip.text: modelData.label
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("%n applications", "", view.backend.filteredApplications.count)
                color: view.paletteColors.muted
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Text { text: filter.text.trim().length > 0 ? qsTr("Best match") : qsTr("Name · A–Z"); color: view.paletteColors.muted; font.pixelSize: 12 }
        }
        GridView {
            id: apps
            objectName: "applicationsGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: view.backend.filteredApplications
            cellWidth: view.listMode ? width : width / Math.max(1, Math.floor(width / 124))
            cellHeight: view.listMode ? 66 : 120
            cacheBuffer: cellHeight * 2
            keyNavigationWraps: true
            highlightMoveDuration: 0
            Keys.onReturnPressed: view.openCurrent()
            Keys.onEnterPressed: view.openCurrent()
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            delegate: Item {
                id: cell
                required property string desktopId
                required property string appName
                required property string appIcon
                required property string genericName
                required property string comment
                required property int index
                width: apps.cellWidth
                height: apps.cellHeight
                Controls.ItemDelegate {
                    id: card
                    objectName: "application_" + cell.desktopId
                    anchors.fill: parent
                    anchors.margins: 4
                    hoverEnabled: true
                    Accessible.name: cell.appName
                    Accessible.description: cell.comment || cell.genericName
                    onClicked: view.backend.launchApplication(cell.desktopId)
                    // Hover is visual only; changing currentIndex would make
                    // GridView scroll partially visible cards into view.
                    background: Rectangle {
                        radius: 8
                        color: card.down ? Qt.alpha(view.paletteColors.accent, 0.22)
                            : card.hovered || (apps.activeFocus && cell.GridView.isCurrentItem)
                            ? Qt.alpha(view.paletteColors.text, 0.08) : Qt.alpha(view.paletteColors.surface, 0.45)
                        border.width: card.activeFocus || (apps.activeFocus && cell.GridView.isCurrentItem) ? 1 : 0
                        border.color: view.paletteColors.accent
                    }
                    contentItem: Item {
                        Kirigami.Icon {
                            id: appImage
                            source: cell.appIcon
                            width: view.listMode ? 32 : 46
                            height: width
                            x: view.listMode ? 10 : (parent.width - width) / 2
                            y: view.listMode ? (parent.height - height) / 2 : 8
                        }
                        Text {
                            id: name
                            text: cell.appName
                            textFormat: Text.PlainText
                            color: view.paletteColors.text
                            font.pixelSize: 13
                            x: view.listMode ? 56 : 3
                            y: view.listMode ? 7 : 62
                            width: parent.width - x - (view.listMode ? 10 : 3)
                            horizontalAlignment: view.listMode ? Text.AlignLeft : Text.AlignHCenter
                            wrapMode: Text.Wrap
                            maximumLineCount: view.listMode ? 1 : 2
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: view.listMode
                            text: cell.genericName || cell.comment
                            textFormat: Text.PlainText
                            color: view.paletteColors.muted
                            font.pixelSize: 11
                            x: 56
                            y: 28
                            width: parent.width - x - 10
                            elide: Text.ElideRight
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: point => {
                            view.showApplicationMenu(cell.desktopId, card, point.position.x, point.position.y)
                        }
                    }
                    Keys.onMenuPressed: view.showApplicationMenu(cell.desktopId, card, 16, height / 2)
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: apps.count === 0
                text: view.backend.applications.scanning ? qsTr("Loading applications…")
                    : filter.text.trim().length > 0 ? qsTr("No applications match your filter")
                    : qsTr("No applications found")
                color: view.paletteColors.muted
                font.pixelSize: 13
            }
        }
    }
    RevealMenu {
        id: appMenu
        objectName: "applicationContextMenu"
        actionObjectName: "revealApplication"
        paletteColors: view.paletteColors
        backend: view.backend
        onClosed: view.contextDesktopId = ""
        onRevealRequested: view.backend.revealPath(targetPath)
    }
}
