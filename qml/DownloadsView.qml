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
    signal backRequested()
    function focusSearch() { filter.forceActiveFocus() }
    function begin() {
        filter.clear()
        files.positionViewAtBeginning()
        filter.forceActiveFocus()
    }
    function escapeSearch() {
        if (filter.text.length > 0) {
            filter.clear()
            filter.forceActiveFocus()
        } else backend.dismiss()
    }
    function openCurrent() {
        const path = backend.filteredDownloads.pathAt(files.currentIndex)
        if (path) backend.openDownload(path)
    }
    Keys.onEscapePressed: escapeSearch()
    Connections {
        target: view.backend.filteredDownloads
        function onQueryChanged() {
            files.currentIndex = view.backend.filteredDownloads.count > 0 ? 0 : -1
            files.positionViewAtBeginning()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Controls.TextField {
            id: filter
            objectName: "downloadsSearchField"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            placeholderText: qsTr("Search recent downloads…")
            placeholderTextColor: view.paletteColors.muted
            color: view.paletteColors.text
            selectionColor: view.paletteColors.accent
            selectedTextColor: view.paletteColors.surface
            selectByMouse: true
            font.pixelSize: 14
            leftPadding: 40
            rightPadding: 36
            onTextChanged: view.backend.filteredDownloads.query = text
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
                id: clearButton
                objectName: "clearDownloadsSearch"
                visible: filter.text.length > 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32
                Accessible.name: qsTr("Clear downloads search")
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
            Keys.onDownPressed: files.incrementCurrentIndex()
            Keys.onUpPressed: files.decrementCurrentIndex()
            Keys.onReturnPressed: view.openCurrent()
            Keys.onEnterPressed: view.openCurrent()
        }
        Text {
            Layout.fillWidth: true
            text: view.backend.downloads.directory
            textFormat: Text.PlainText
            color: view.paletteColors.muted
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: view.backend.downloads.error
            color: "#ffae9e"
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        ListView {
            id: files
            objectName: "downloadsList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: view.backend.filteredDownloads
            reuseItems: true
            keyNavigationWraps: true
            currentIndex: -1
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            Keys.onReturnPressed: view.openCurrent()
            Keys.onEnterPressed: view.openCurrent()
            delegate: Controls.ItemDelegate {
                id: fileRow
                required property string fileName
                required property string filePath
                required property string fileIcon
                required property string description
                required property int index
                width: files.width
                height: 64
                hoverEnabled: true
                Accessible.name: fileName
                Accessible.description: description
                onClicked: view.backend.openDownload(filePath)
                onHoveredChanged: { if (hovered) files.currentIndex = index }
                background: Rectangle {
                    radius: 7
                    color: fileRow.down ? Qt.alpha(view.paletteColors.accent, 0.22)
                        : fileRow.hovered || fileRow.ListView.isCurrentItem
                        ? Qt.alpha(view.paletteColors.text, 0.08) : "transparent"
                    border.width: fileRow.activeFocus ? 1 : 0
                    border.color: view.paletteColors.accent
                }
                contentItem: RowLayout {
                    spacing: 12
                    Kirigami.Icon { source: fileRow.fileIcon; Layout.preferredWidth: 32; Layout.preferredHeight: 32 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            Layout.fillWidth: true
                            text: fileRow.fileName
                            textFormat: Text.PlainText
                            color: view.paletteColors.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: fileRow.description
                            textFormat: Text.PlainText
                            color: view.paletteColors.muted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: (point, button) => {
                        files.currentIndex = fileRow.index
                        contextMenu.targetPath = fileRow.filePath
                        contextMenu.pinItem = view.backend.fileItem(fileRow.filePath)
                        contextMenu.popup(fileRow, point.position.x, point.position.y)
                    }
                }
                Keys.onMenuPressed: {
                    contextMenu.targetPath = filePath
                    contextMenu.pinItem = view.backend.fileItem(filePath)
                    contextMenu.popup(fileRow, 16, height / 2)
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: view.backend.filteredDownloads.count === 0
                text: view.backend.downloads.scanning ? qsTr("Checking Downloads…")
                    : filter.text.trim().length > 0 ? qsTr("No downloads match your search")
                    : qsTr("Nothing in Downloads yet")
                color: view.paletteColors.muted
                font.pixelSize: 13
            }
        }
    }
    RevealMenu {
        id: contextMenu
        objectName: "downloadContextMenu"
        actionObjectName: "revealDownload"
        paletteColors: view.paletteColors
        backend: view.backend
        onRevealRequested: view.backend.revealDownload(targetPath)
    }
}
