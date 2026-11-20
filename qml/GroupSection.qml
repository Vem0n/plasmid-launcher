// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Controls
import org.kde.kirigami as Kirigami

Item {
    id: section
    required property var backend
    required property var paletteColors
    property bool expanded: false
    property real maximumGalleryHeight: 340
    readonly property int columns: Math.max(1, Math.floor(width / 88))
    readonly property real galleryHeight: expanded
        ? Math.min(maximumGalleryHeight, Math.max(90, Math.ceil(backend.groups.items.length / columns) * 90)) : 90
    signal browseApplications()
    // The panel drives the reveal height; the GridView itself never resizes.
    property real revealedGalleryHeight: galleryHeight
    property bool resizing: false
    property bool keepTailVisible: false
    implicitHeight: galleryFrame.y + revealedGalleryHeight

    function toggleGallery() {
        gallery.cancelFlick()
        rewindGallery.stop()
        expanded = !expanded
        if (!expanded) {
            rewindGallery.from = gallery.contentY
            rewindGallery.to = gallery.originY
            rewindGallery.start()
        }
    }
    function scrollGroups(toEnd) {
        keepTailVisible = toEnd
        groupTabs.cancelFlick()
        scrollTabs.stop()
        scrollTabs.from = groupTabs.contentX
        scrollTabs.to = toEnd ? groupTabs.maximumX : 0
        scrollTabs.start()
    }
    function fitTabs() {
        if (keepTailVisible) scrollGroups(true)
        else {
            scrollTabs.stop()
            groupTabs.returnToBounds()
        }
    }
    function revealSelectedGroup() {
        if (keepTailVisible) { scrollGroups(true); return }
        const selected = backend.groups.selectedId
        for (let i = 0; i < tabRepeater.count; ++i) {
            const tab = tabRepeater.itemAt(i)
            if (!tab || tab.objectName !== "groupTab_" + selected) continue
            const left = groupTabs.contentX + (groupTabs.contentX > 1 ? 28 : 0)
            const right = groupTabs.contentX + groupTabs.width - (groupTabs.contentX < groupTabs.maximumX - 1 ? 28 : 0)
            if (tab.x >= left && tab.x + tab.width <= right) return
            groupTabs.cancelFlick()
            scrollTabs.stop()
            scrollTabs.from = groupTabs.contentX
            scrollTabs.to = Math.max(0, Math.min(groupTabs.maximumX, tab.x < left ? tab.x - 28 : tab.x + tab.width + 28 - groupTabs.width))
            scrollTabs.start()
            return
        }
    }

    function reset() {
        rewindGallery.stop()
        expanded = false
        gallery.positionViewAtBeginning()
        editor.close(); groupMenu.close(); itemMenu.close()
    }
    function focusEditor() {
        if (!editor.visible) return false
        groupName.forceActiveFocus()
        return true
    }
    function editGroup(id, name) {
        editor.targetId = id
        groupName.text = name
        editor.open()
        groupName.forceActiveFocus()
        groupName.selectAll()
    }

    RowLayout {
        id: groupHeader
        anchors.left: parent.left
        anchors.right: parent.right
        height: 32
        spacing: 8
        Item {
            id: strip
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: 32
            Flickable {
                id: groupTabs
                objectName: "groupTabs"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 28
                contentWidth: tabs.width
                contentHeight: height
                readonly property real maximumX: Math.max(0, contentWidth - width)
                readonly property bool overflowing: maximumX > 1
                clip: true
                pixelAligned: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                onDraggingChanged: if (dragging) { section.keepTailVisible = false; scrollTabs.stop() }
                onContentWidthChanged: Qt.callLater(section.fitTabs)
                onWidthChanged: Qt.callLater(section.fitTabs)
                Row {
                    id: tabs
                    spacing: 3
                    Repeater {
                        id: tabRepeater
                        model: section.backend.groups.groups
                        Controls.Button {
                            id: tab
                            required property var modelData
                            text: modelData.name
                            objectName: "groupTab_" + modelData.id
                            height: 28
                            width: Math.min(140, Math.max(62, implicitWidth))
                            checked: section.backend.groups.selectedId === modelData.id
                            onClicked: section.backend.groups.selectedId = modelData.id
                            contentItem: Text { text: tab.text; textFormat: Text.PlainText; color: tab.checked ? section.paletteColors.text : section.paletteColors.muted; font.pixelSize: 12; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: 5; color: tab.checked ? Qt.alpha(section.paletteColors.accent, 0.16) : tab.hovered ? Qt.alpha(section.paletteColors.text, 0.06) : "transparent" }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: point => {
                                    groupMenu.targetId = tab.modelData.id
                                    groupMenu.targetName = tab.modelData.name
                                    const p = tab.mapToItem(section, point.position.x, point.position.y)
                                    groupMenu.popup(section, p.x, p.y)
                                }
                            }
                        }
                    }
                    PanelButton {
                        id: createGroup
                        objectName: "createGroup"
                        paletteColors: section.paletteColors
                        height: 28
                        width: 28
                        flat: true
                        text: "+"
                        Accessible.name: qsTr("Create group")
                        onClicked: {
                            const id = section.backend.groups.create(qsTr("New group"))
                            if (id) {
                                section.keepTailVisible = true
                                section.editGroup(id, qsTr("New group"))
                                Qt.callLater(() => section.scrollGroups(true))
                            }
                        }
                    }
                }
                Controls.ScrollBar.horizontal: Controls.ScrollBar {
                    id: positionBar
                    objectName: "groupPositionBar"
                    parent: strip
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 4
                    padding: 0
                    policy: groupTabs.overflowing ? Controls.ScrollBar.AlwaysOn : Controls.ScrollBar.AlwaysOff
                    minimumSize: 0.08
                    onPressedChanged: if (pressed) { section.keepTailVisible = false; scrollTabs.stop() }
                    background: Rectangle { radius: 2; color: Qt.alpha(section.paletteColors.text, 0.09) }
                    contentItem: Rectangle {
                        objectName: "groupPositionThumb"
                        radius: 2
                        color: Qt.alpha(section.paletteColors.accent, positionBar.pressed || positionBar.hovered ? 0.95 : 0.65)
                    }
                }
            }
            PanelButton {
                objectName: "groupsFirst"
                paletteColors: section.paletteColors
                anchors.left: parent.left
                anchors.top: parent.top
                width: 28
                height: 28
                visible: groupTabs.overflowing && groupTabs.contentX > 1
                Accessible.name: qsTr("Scroll to first groups")
                contentItem: Kirigami.Icon { source: "go-previous"; color: section.paletteColors.text; implicitWidth: 16; implicitHeight: 16 }
                background: Rectangle { radius: 5; color: section.paletteColors.background; border.color: Qt.alpha(section.paletteColors.accent, 0.3) }
                onClicked: section.scrollGroups(false)
            }
            PanelButton {
                objectName: "groupsLast"
                paletteColors: section.paletteColors
                anchors.right: parent.right
                anchors.top: parent.top
                width: 28
                height: 28
                visible: groupTabs.overflowing && groupTabs.contentX < groupTabs.maximumX - 1
                Accessible.name: qsTr("Scroll to last groups")
                contentItem: Kirigami.Icon { source: "go-next"; color: section.paletteColors.text; implicitWidth: 16; implicitHeight: 16 }
                background: Rectangle { radius: 5; color: section.paletteColors.background; border.color: Qt.alpha(section.paletteColors.accent, 0.3) }
                onClicked: section.scrollGroups(true)
            }
        }
        PanelButton {
            objectName: "browseApplications"
            paletteColors: section.paletteColors
            flat: true
            text: qsTr("Applications")
            Accessible.name: qsTr("Browse applications")
            onClicked: section.browseApplications()
        }
    }
    NumberAnimation {
        id: scrollTabs
        objectName: "groupScrollAnimation"
        target: groupTabs
        property: "contentX"
        duration: 300
        easing.type: Easing.InOutCubic
    }
    RowLayout {
        id: galleryHeader
        anchors.top: groupHeader.bottom
        anchors.topMargin: 6
        anchors.left: parent.left
        anchors.right: parent.right
        height: 24
        Text {
            objectName: "groupItemCount"
            Layout.fillWidth: true
            text: qsTr("%n items", "", section.backend.groups.items.length)
            color: section.paletteColors.muted
            font.pixelSize: 11
        }
        PanelButton {
            id: expand
            objectName: "expandGroup"
            paletteColors: section.paletteColors
            flat: true
            text: section.expanded ? qsTr("Show less") : qsTr("Show all")
            Accessible.name: section.expanded ? qsTr("Collapse group") : qsTr("Expand group")
            visible: section.expanded || section.backend.groups.items.length > section.columns
            Layout.preferredHeight: 24
            rightPadding: 28
            onClicked: section.toggleGallery()
            Kirigami.Icon {
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                source: section.expanded ? "arrow-up" : "arrow-down"
                color: section.paletteColors.text
                width: 14
                height: 14
            }
        }
    }
    Item {
        id: galleryFrame
        objectName: "groupGalleryFrame"
        anchors.top: galleryHeader.bottom
        anchors.topMargin: 4
        width: parent.width
        height: section.revealedGalleryHeight
        clip: true
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: Qt.alpha(section.paletteColors.text, 0.025)
            border.color: Qt.alpha(section.paletteColors.text, 0.07)
        }
        GridView {
            id: gallery
            objectName: "groupGallery"
            width: parent.width
            height: Math.max(90, section.maximumGalleryHeight)
            clip: true
            model: section.backend.groups.items
            cellWidth: width / section.columns
            cellHeight: 90
            interactive: section.expanded && !section.resizing
            reuseItems: true
            delegate: AppTile {
                id: tile
                required property var modelData
                required property int index
                width: gallery.cellWidth
                height: gallery.cellHeight
                // Off-clip rows must not enter the collapsed tab-focus chain.
                enabled: !section.resizing && (section.expanded || index < section.columns)
                // Retain existing object names for favorite keyboard/UI checks.
                objectName: modelData.kind === "app" ? "favorite_" + modelData.desktopId : "groupItem_" + index
                label: modelData.name
                iconName: modelData.icon
                paletteColors: section.paletteColors
                onClicked: section.backend.launchGroupItem(section.backend.groups.selectedId, modelData.key)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: point => {
                        itemMenu.pinItem = tile.modelData
                        itemMenu.removeGroupId = section.backend.groups.selectedId
                        itemMenu.targetPath = tile.modelData.kind === "file" ? tile.modelData.path : ""
                        const p = tile.mapToItem(section, point.position.x, point.position.y)
                        itemMenu.popup(section, p.x, p.y)
                        if (tile.modelData.kind === "app") section.backend.requestApplicationLocation(tile.modelData.desktopId)
                    }
                }
                Keys.onMenuPressed: {
                    itemMenu.pinItem = modelData
                    itemMenu.removeGroupId = section.backend.groups.selectedId
                    itemMenu.targetPath = modelData.path || ""
                    const p = tile.mapToItem(section, 10, height / 2)
                    itemMenu.popup(section, p.x, p.y)
                    if (modelData.kind === "app") section.backend.requestApplicationLocation(modelData.desktopId)
                }
            }
            Controls.ScrollBar.vertical: Controls.ScrollBar {
                policy: section.expanded || section.resizing ? Controls.ScrollBar.AsNeeded : Controls.ScrollBar.AlwaysOff
            }
        }
        Text {
            anchors.centerIn: parent
            visible: section.backend.groups.items.length === 0
            width: parent.width - 24
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("This group is empty.")
            color: section.paletteColors.muted
            font.pixelSize: 12
        }
    }
    NumberAnimation {
        id: rewindGallery
        target: gallery
        property: "contentY"
        duration: 230
        easing.type: Easing.InOutCubic
    }
    Connections {
        target: section.backend.groups
        function onSelectionChanged() {
            section.keepTailVisible = false
            rewindGallery.stop()
            gallery.positionViewAtBeginning()
            Qt.callLater(section.revealSelectedGroup)
        }
    }
    Connections {
        target: section.backend
        function onApplicationLocationReady(desktopId, path, label) {
            if (!itemMenu.visible || itemMenu.pinItem.desktopId !== desktopId) return
            itemMenu.targetPath = path
            itemMenu.locationLabel = label
        }
    }
    RevealMenu {
        id: itemMenu
        objectName: "groupItemMenu"
        backend: section.backend
        paletteColors: section.paletteColors
        onRevealRequested: section.backend.revealPath(targetPath)
    }
    Controls.Menu {
        id: groupMenu
        property string targetId: ""
        property string targetName: ""
        popupType: Controls.Popup.Item
        width: 180
        padding: 5
        background: Rectangle { radius: 8; color: section.paletteColors.surface; border.color: Qt.alpha(section.paletteColors.text, 0.18) }
        Controls.MenuItem {
            id: renameGroup
            text: qsTr("Rename group")
            enabled: groupMenu.targetId !== "pinned"
            contentItem: Text { text: renameGroup.text; color: renameGroup.enabled ? section.paletteColors.text : section.paletteColors.muted; font.pixelSize: 13; leftPadding: 8 }
            background: Rectangle { radius: 5; color: renameGroup.highlighted ? Qt.alpha(section.paletteColors.accent, 0.18) : "transparent" }
            onTriggered: section.editGroup(groupMenu.targetId, groupMenu.targetName)
        }
        Controls.MenuItem {
            id: deleteGroup
            text: qsTr("Delete group")
            enabled: groupMenu.targetId !== "pinned"
            contentItem: Text { text: deleteGroup.text; color: deleteGroup.enabled ? section.paletteColors.text : section.paletteColors.muted; font.pixelSize: 13; leftPadding: 8 }
            background: Rectangle { radius: 5; color: deleteGroup.highlighted ? Qt.alpha(section.paletteColors.accent, 0.18) : "transparent" }
            onTriggered: section.backend.groups.remove(groupMenu.targetId)
        }
    }
    Controls.Popup {
        id: editor
        objectName: "groupNameEditor"
        property string targetId: ""
        popupType: Controls.Popup.Item
        x: Math.max(0, (section.width - width) / 2)
        y: 28
        width: Math.min(320, section.width)
        padding: 14
        modal: true
        dim: false
        focus: true
        background: Rectangle { radius: 8; color: section.paletteColors.surface; border.color: Qt.alpha(section.paletteColors.text, 0.2) }
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: qsTr("Group name"); color: section.paletteColors.text; font.pixelSize: 13 }
            Controls.TextField {
                id: groupName
                objectName: "groupNameField"
                Layout.fillWidth: true
                maximumLength: 80
                color: section.paletteColors.text
                background: Rectangle { color: section.paletteColors.background; radius: 5; border.color: section.paletteColors.accent }
                onAccepted: saveName.clicked()
            }
            PanelButton {
                id: saveName
                paletteColors: section.paletteColors
                objectName: "saveGroupName"
                text: qsTr("Save")
                enabled: groupName.text.trim().length > 0
                onClicked: { if (section.backend.groups.rename(editor.targetId, groupName.text)) editor.close() }
            }
        }
    }
}
