// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as Controls

Controls.Menu {
    id: menu
    required property var paletteColors
    property string targetPath: ""
    property string locationLabel: ""
    property string actionObjectName: "revealLocation"
    property bool busy: false
    property var backend: null
    property var pinItem: ({})
    readonly property var groupSnapshot: backend ? backend.groups.groups : []
    property string removeGroupId: ""
    signal revealRequested()
    popupType: Controls.Popup.Item
    width: 280
    padding: 5
    delegate: Controls.MenuItem {
        id: groupOpener
        objectName: "addToGroup"
        implicitHeight: 38
        contentItem: Text {
            objectName: "addToGroupLabel"
            text: groupOpener.text
            textFormat: Text.PlainText
            color: groupOpener.enabled ? menu.paletteColors.text : menu.paletteColors.muted
            font.pixelSize: 13
            leftPadding: 8
            rightPadding: 20
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle { radius: 5; color: groupOpener.highlighted ? Qt.alpha(menu.paletteColors.accent, 0.18) : "transparent" }
    }
    palette.windowText: paletteColors.text
    palette.text: paletteColors.text
    palette.buttonText: paletteColors.text
    palette.window: paletteColors.surface
    palette.light: Qt.alpha(paletteColors.accent, 0.18)
    palette.midlight: Qt.alpha(paletteColors.accent, 0.24)
    background: Rectangle {
        radius: 8
        color: menu.paletteColors.surface
        border.width: 1
        border.color: Qt.alpha(menu.paletteColors.text, 0.18)
    }
    Controls.MenuItem {
        id: pin
        objectName: "pinContextItem"
        visible: menu.backend !== null && !!menu.pinItem.key
        enabled: visible && menu.groupSnapshot.length > 0 && !menu.backend.groups.contains("pinned", menu.pinItem.key)
        text: enabled ? qsTr("Pin") : qsTr("Pinned")
        implicitHeight: visible ? 38 : 0
        contentItem: Text { text: pin.text; color: pin.enabled ? menu.paletteColors.text : menu.paletteColors.muted; font.pixelSize: 13; leftPadding: 8; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 5; color: pin.highlighted ? Qt.alpha(menu.paletteColors.accent, 0.18) : "transparent" }
        onTriggered: menu.backend.groups.add("pinned", menu.pinItem)
    }
    Controls.Menu {
        id: groupTargets
        objectName: "groupTargetsMenu"
        // The custom background has no implicit width. Without an explicit
        // width Qt opens a zero-width submenu, even though visible is true.
        width: menu.width
        title: qsTr("Add to group")
        enabled: menu.backend !== null && !!menu.pinItem.key
        popupType: Controls.Popup.Item
        palette.windowText: menu.paletteColors.text
        palette.text: menu.paletteColors.text
        palette.window: menu.paletteColors.surface
        background: Rectangle { radius: 8; color: menu.paletteColors.surface; border.color: Qt.alpha(menu.paletteColors.text, 0.18) }
        Instantiator {
            model: menu.backend ? menu.backend.groups.groups : []
            delegate: Controls.MenuItem {
                id: targetGroup
                required property var modelData
                objectName: "groupTarget_" + modelData.id
                text: modelData.name
                readonly property bool alreadyMember: menu.backend !== null && menu.groupSnapshot.length > 0
                    && menu.backend.groups.contains(modelData.id, menu.pinItem.key || "")
                enabled: !alreadyMember
                Accessible.description: alreadyMember ? qsTr("Already in this group") : qsTr("Add to this group")
                contentItem: Item {
                    implicitHeight: 22
                    Text {
                        objectName: "groupCheck_" + targetGroup.modelData.id
                        text: "✓"
                        visible: targetGroup.alreadyMember
                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        color: menu.paletteColors.accent
                        font.pixelSize: 15
                    }
                    Text {
                        text: targetGroup.text
                        textFormat: Text.PlainText
                        x: 30
                        width: parent.width - x - 8
                        anchors.verticalCenter: parent.verticalCenter
                        color: targetGroup.enabled ? menu.paletteColors.text : menu.paletteColors.muted
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }
                background: Rectangle { radius: 5; color: targetGroup.highlighted ? Qt.alpha(menu.paletteColors.accent, 0.18) : "transparent" }
                onTriggered: menu.backend.groups.add(modelData.id, menu.pinItem)
            }
            onObjectAdded: (index, object) => groupTargets.insertItem(index, object)
            onObjectRemoved: (index, object) => groupTargets.removeItem(object)
        }
    }
    Controls.MenuItem {
        id: unpin
        visible: !!menu.removeGroupId && !!menu.pinItem.key
        text: qsTr("Remove from group")
        implicitHeight: visible ? 38 : 0
        contentItem: Text { text: unpin.text; color: menu.paletteColors.text; font.pixelSize: 13; leftPadding: 8; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 5; color: unpin.highlighted ? Qt.alpha(menu.paletteColors.accent, 0.18) : "transparent" }
        onTriggered: menu.backend.groups.unpin(menu.removeGroupId, menu.pinItem.key)
    }
    Controls.MenuItem {
        id: reveal
        objectName: menu.actionObjectName
        text: qsTr("Show in File Explorer")
        enabled: !menu.busy && menu.targetPath.length > 0
        implicitHeight: 38
        contentItem: Text {
            text: reveal.text
            color: reveal.enabled ? menu.paletteColors.text : menu.paletteColors.muted
            font.pixelSize: 13
            leftPadding: 8
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { radius: 5; color: reveal.highlighted ? Qt.alpha(menu.paletteColors.accent, 0.18) : "transparent" }
        onTriggered: menu.revealRequested()
    }
}
