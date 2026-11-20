// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Controls
import org.kde.kirigami as Kirigami

Rectangle {
    id: panel
    objectName: "launcherPanel"
    required property var backend
    readonly property var colors: backend.theme
    readonly property bool searching: search.text.trim().length > 0
    readonly property var results: resultsLoader.item
    property string page: "home"
    property bool returningToMain: false
    property bool resetting: false
    property bool transitioning: false
    property string skeletonPage: "home"
    readonly property bool downloadsExpanded: page === "downloads"
    readonly property bool applicationsExpanded: page === "applications"
    readonly property bool expanded: page !== "home"
    property real maximumHeight: 680
    property real maximumWidth: 1000
    readonly property real expandedHeight: Math.min(620, maximumHeight)
    readonly property real applicationsHeight: Math.min(704, maximumHeight)
    readonly property bool resizing: heightAnimation.running || widthAnimation.running
    readonly property real contentWidth: Math.min(page === "applications" ? 976 : 616, maximumWidth)
    readonly property real compactHomeHeight: Math.min(maximumHeight, 412)
    readonly property real homeHeight: Math.min(maximumHeight, compactHomeHeight + Math.max(0, groupSection.galleryHeight - 90))
    implicitHeight: !returningToMain && page === "applications" ? applicationsHeight
        : !returningToMain && page === "downloads" ? expandedHeight : homeHeight
    implicitWidth: returningToMain ? Math.min(616, maximumWidth) : contentWidth
    height: implicitHeight
    width: implicitWidth
    Behavior on height { enabled: !panel.resetting; NumberAnimation { id: heightAnimation; duration: 230; easing.type: Easing.InOutCubic } }
    Behavior on width { enabled: !panel.resetting; NumberAnimation { id: widthAnimation; duration: 230; easing.type: Easing.InOutCubic } }
    clip: true
    radius: 12
    color: colors.background
    border.width: 1
    border.color: Qt.alpha(colors.text, 0.16)
    property string errorMessage: ""
    Kirigami.Theme.inherit: false
    Kirigami.Theme.textColor: colors.text
    Kirigami.Theme.backgroundColor: colors.background
    Kirigami.Theme.highlightColor: colors.accent
    Kirigami.Theme.highlightedTextColor: colors.surface

    function reset() {
        resetting = true
        revealAnimation.stop()
        resizeSkeleton.opacity = 0
        transitioning = false
        groupSection.reset()
        suggestionPopup.close()
        feedback.clear()
        errorMessage = ""
        page = "home"
        returningToMain = false
        search.text = ""
        resetting = false
        search.forceActiveFocus()
    }
    function clearQuery() { search.text = "" }
    function focusSearch() { search.forceActiveFocus() }
    function escapeSearch() {
        if (search.text.length > 0) search.clear()
        else backend.dismiss()
    }
    function showDownloads() {
        beginTransition("downloads")
        search.clear()
        page = "downloads"
        backend.downloads.refresh()
        recentDownloads.begin()
        Qt.callLater(settleTransition)
    }
    function goBack() {
        if (returningToMain || !expanded) return
        beginTransition("home")
        returningToMain = true
        Qt.callLater(settleTransition)
    }
    function finishReturn() {
        if (!returningToMain || resizing) return
        page = "home"
        returningToMain = false
    }
    function showApplications() {
        beginTransition("applications")
        search.clear()
        page = "applications"
        applications.begin()
        Qt.callLater(settleTransition)
    }
    function focusCurrentSearch() {
        if (downloadsExpanded) recentDownloads.focusSearch()
        else if (applicationsExpanded) applications.focusSearch()
        else if (!groupSection.focusEditor()) search.forceActiveFocus()
    }
    function beginTransition(destination) {
        revealAnimation.stop()
        skeletonPage = destination
        transitioning = true
        resizeSkeleton.opacity = 1
        panel.forceActiveFocus()
    }
    function settleTransition() {
        if (resetting || !transitioning || resizing || revealAnimation.running) return
        finishReturn()
        // Let the destination layout settle underneath the opaque skeleton.
        Qt.callLater(revealTransition)
    }
    function revealTransition() {
        if (!resetting && transitioning && !resizing && !revealAnimation.running)
            revealAnimation.start()
    }
    Connections {
        target: panel.backend
        function onLaunchError(message) { panel.errorMessage = message }
        function onClosing() { feedback.clear() }
        function onExternalClosing() { feedback.clear() }
    }
    Connections {
        target: panel.backend.groups
        function onItemAdded(message) { feedback.showMessage(message) }
    }
    Connections {
        target: panel
        function onResizingChanged() {
            if (panel.resetting) return
            if (panel.resizing) {
                // Inline groups reveal their real gallery through the resize.
                // Skeletons are reserved for transitions between screens.
                if (panel.page === "home" && !panel.returningToMain && !panel.transitioning) return
                if (!panel.transitioning || revealAnimation.running)
                    panel.beginTransition(panel.returningToMain ? "home" : panel.page)
            } else Qt.callLater(panel.settleTransition)
        }
    }

    ColumnLayout {
        id: panelLayout
        objectName: "panelContent"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.margins: 22
        // Keep content at its destination width while the panel clip animates.
        width: panel.contentWidth - 44
        enabled: !panel.transitioning
        opacity: 1 - resizeSkeleton.opacity
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Controls.ToolButton {
                id: backButton
                objectName: "downloadsBack"
                visible: panel.expanded
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                Accessible.name: qsTr("Back to Plasmid")
                onClicked: panel.goBack()
                contentItem: Kirigami.Icon { source: "go-previous"; color: panel.colors.text }
                background: Rectangle { radius: 6; color: backButton.hovered ? Qt.alpha(panel.colors.text, 0.08) : "transparent" }
            }
            Kirigami.Icon { source: "view-grid"; Layout.preferredWidth: 18; Layout.preferredHeight: 18; color: panel.colors.accent }
            Text { text: panel.applicationsExpanded ? qsTr("Applications") : panel.downloadsExpanded ? qsTr("Recently downloaded") : "Plasmid"; color: panel.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Text { text: panel.backend.shortcutEnabled ? "Alt + Shift" : qsTr("No hotkey"); color: panel.colors.muted; font.pixelSize: 11 }
            Controls.ToolButton {
                id: closeButton
                text: "×"
                Accessible.name: qsTr("Close Plasmid")
                Layout.preferredWidth: 26
                Layout.preferredHeight: 24
                onClicked: panel.backend.dismiss()
                contentItem: Text { text: closeButton.text; color: panel.colors.muted; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 5; color: closeButton.hovered ? Qt.alpha(panel.colors.text, 0.08) : "transparent" }
            }
        }

        Controls.TextField {
            id: search
            visible: !panel.expanded
            objectName: "searchField"
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            leftPadding: 43
            rightPadding: 36
            color: panel.colors.text
            selectionColor: panel.colors.accent
            selectedTextColor: panel.colors.surface
            placeholderTextColor: panel.colors.muted
            placeholderText: qsTr("Search apps, files, commands…")
            font.pixelSize: 15
            selectByMouse: true
            background: Rectangle {
                radius: 7
                color: panel.colors.surface
                border.width: 1
                border.color: search.activeFocus ? Qt.alpha(panel.colors.accent, 0.55) : Qt.alpha(panel.colors.text, 0.12)
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 2
                    height: 2
                    radius: 1
                    color: panel.colors.accent
                    visible: search.activeFocus
                }
            }
            Kirigami.Icon { source: "system-search"; x: 14; anchors.verticalCenter: parent.verticalCenter; width: 19; height: 19; color: panel.colors.muted }
            Controls.ToolButton {
                id: clearButton
                visible: search.text.length > 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32
                text: "×"
                Accessible.name: qsTr("Clear search")
                onClicked: { search.clear(); search.forceActiveFocus() }
                contentItem: Text { text: clearButton.text; color: panel.colors.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 18 }
                background: Item {}
            }
            Keys.onEscapePressed: panel.escapeSearch()
            Keys.onReturnPressed: event => { if (panel.results) panel.results.runCurrentIndex(event) }
            Keys.onEnterPressed: event => { if (panel.results) panel.results.runCurrentIndex(event) }
            Keys.onDownPressed: event => {
                if (panel.results) panel.results.incrementCurrentIndex()
                else nextItemInFocusChain().forceActiveFocus()
                event.accepted = true
            }
            Keys.onUpPressed: event => { if (panel.results) panel.results.decrementCurrentIndex(); event.accepted = true }
            Keys.onTabPressed: event => {
                nextItemInFocusChain().forceActiveFocus()
                event.accepted = true
            }
        }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                anchors.fill: parent
                visible: !panel.searching && !panel.expanded
                spacing: 8
                GroupSection {
                    id: groupSection
                    objectName: "groupSection"
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    Layout.minimumHeight: implicitHeight
                    Layout.maximumHeight: implicitHeight
                    maximumGalleryHeight: Math.max(90, Math.min(360, panel.maximumHeight - panel.compactHomeHeight + 90))
                    revealedGalleryHeight: Math.max(90, Math.min(maximumGalleryHeight,
                        90 + panel.height - panel.compactHomeHeight))
                    resizing: panel.resizing
                    backend: panel.backend
                    paletteColors: panel.colors
                    onBrowseApplications: panel.showApplications()
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Qt.alpha(panel.colors.text, 0.08) }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: panel.backend.places.slice(0, 2)
                        AppTile {
                            id: placeTile
                            required property var modelData
                            required property int index
                            objectName: "place_" + modelData.id
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            compact: true
                            label: modelData.name
                            iconName: modelData.icon
                            paletteColors: panel.colors
                            onClicked: panel.backend.openPlace(modelData.id)
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: point => {
                                    placeMenu.pinItem = panel.backend.fileItem(placeTile.modelData.path)
                                    placeMenu.targetPath = placeTile.modelData.path
                                    const p = placeTile.mapToItem(panel, point.position.x, point.position.y)
                                    placeMenu.popup(panel, p.x, p.y)
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 0
                        AppTile {
                            id: downloadsTile
                            objectName: "openDownloads"
                            Layout.fillWidth: true
                            compact: true
                            label: qsTr("Downloads")
                            iconName: "folder-downloads"
                            paletteColors: panel.colors
                            onClicked: panel.backend.openDownloads()
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: point => {
                                    placeMenu.pinItem = panel.backend.fileItem(panel.backend.downloads.directory)
                                    placeMenu.targetPath = panel.backend.downloads.directory
                                    const p = downloadsTile.mapToItem(panel, point.position.x, point.position.y)
                                    placeMenu.popup(panel, p.x, p.y)
                                }
                            }
                        }
                        Controls.ToolButton {
                            id: expandDownloads
                            objectName: "expandDownloads"
                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 40
                            Accessible.name: qsTr("Show recently downloaded")
                            onClicked: panel.showDownloads()
                            contentItem: Kirigami.Icon { source: "arrow-down"; color: panel.colors.text }
                            background: Rectangle {
                                radius: 6
                                color: expandDownloads.hovered || expandDownloads.activeFocus
                                    ? Qt.alpha(panel.colors.accent, 0.15) : "transparent"
                            }
                            Controls.ToolTip.visible: hovered
                            Controls.ToolTip.text: qsTr("Recently downloaded")
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            Loader {
                id: resultsLoader
                objectName: "searchLoader"
                anchors.fill: parent
                active: panel.backend.searchExecuting || (panel.visible && panel.searching && !panel.expanded)
                source: "SearchResults.qml"
                onLoaded: {
                    item.backend = panel.backend
                    item.inputField = search
                }
            }
            Text {
                anchors.centerIn: parent
                visible: panel.searching && !panel.expanded && (!panel.results || panel.results.count === 0)
                text: !panel.results || panel.results.querying ? qsTr("Searching…") : qsTr("No matches. Try another search.")
                color: panel.colors.muted
                font.pixelSize: 13
            }
            DownloadsView {
                id: recentDownloads
                objectName: "recentDownloads"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                // Lay the list out at its final size once, and reveal it with
                // the body's animated clip instead of relaying out each frame.
                height: Math.max(0, panel.expandedHeight - body.y - panelLayout.anchors.bottomMargin
                                - footer.implicitHeight - panelLayout.spacing)
                visible: panel.downloadsExpanded
                backend: panel.backend
                paletteColors: panel.colors
                onBackRequested: panel.goBack()
            }
            ApplicationsView {
                id: applications
                objectName: "applicationsView"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: Math.max(0, panel.applicationsHeight - body.y - panelLayout.anchors.bottomMargin
                                - footer.implicitHeight - panelLayout.spacing)
                visible: panel.applicationsExpanded
                backend: panel.backend
                paletteColors: panel.colors
            }
        }

        RowLayout {
            id: footer
            Layout.fillWidth: true
            spacing: 6
            Controls.ToolButton {
                id: bell
                objectName: "notificationBell"
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                Accessible.name: panel.backend.groups.notificationsEnabled ? qsTr("Turn off system notifications") : qsTr("Turn on system notifications")
                onClicked: panel.backend.groups.notificationsEnabled = !panel.backend.groups.notificationsEnabled
                contentItem: Kirigami.Icon { source: panel.backend.groups.notificationsEnabled ? "notifications" : "notifications-disabled"; color: panel.backend.groups.notificationsEnabled ? panel.colors.accent : panel.colors.muted }
                background: Rectangle { radius: 5; color: bell.hovered ? Qt.alpha(panel.colors.text, 0.08) : "transparent" }
            }
            Text {
                objectName: "notificationStatus"
                text: qsTr("System Notifications")
                color: panel.colors.muted
                font.pixelSize: 11
            }
            Text {
                objectName: "panelErrorStatus"
                Layout.fillWidth: true
                text: panel.errorMessage || panel.backend.groups.error || panel.backend.applications.libraryError
                    || panel.backend.applications.watchError || panel.backend.shortcutError
                color: "#ffae9e"
                font.pixelSize: 11
                elide: Text.ElideRight
                Accessible.description: text
            }
            PanelButton {
                paletteColors: panel.colors
                flat: true
                visible: panel.backend.groups.suggestions.length > 0 && panel.backend.groups.notificationsEnabled
                text: qsTr("Suggestions (%1)").arg(panel.backend.groups.suggestions.length)
                onClicked: suggestionPopup.open()
            }
        }
    }
    RevealMenu {
        id: placeMenu
        paletteColors: panel.colors
        backend: panel.backend
        onRevealRequested: panel.backend.revealPath(targetPath)
    }
    Controls.Popup {
        id: suggestionPopup
        objectName: "groupSuggestions"
        popupType: Controls.Popup.Item
        x: 22
        y: Math.max(22, panel.height - height - 42)
        width: Math.min(440, panel.width - 44)
        padding: 16
        focus: true
        readonly property var proposal: panel.backend.groups.suggestions.length ? panel.backend.groups.suggestions[0] : ({})
        onProposalChanged: { if (!proposal.id) close() }
        Connections {
            target: panel.backend.groups
            function onNotificationsEnabledChanged() { if (!panel.backend.groups.notificationsEnabled) suggestionPopup.close() }
        }
        background: Rectangle { radius: 8; color: panel.colors.surface; border.color: Qt.alpha(panel.colors.text, 0.2) }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: suggestionPopup.proposal.item ? qsTr("Add %1 to %2?").arg(suggestionPopup.proposal.item.name).arg(suggestionPopup.proposal.groupName) : ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: panel.colors.text
                font.pixelSize: 14
            }
            Text { text: qsTr("Based on the apps you've added to this group."); color: panel.colors.muted; font.pixelSize: 11 }
            RowLayout {
                PanelButton { paletteColors: panel.colors; text: qsTr("Yes"); onClicked: panel.backend.groups.respond(suggestionPopup.proposal.id, "yes") }
                PanelButton { paletteColors: panel.colors; text: qsTr("Later"); onClicked: { panel.backend.groups.respond(suggestionPopup.proposal.id, "later"); suggestionPopup.close() } }
                PanelButton { paletteColors: panel.colors; text: qsTr("Never"); onClicked: panel.backend.groups.respond(suggestionPopup.proposal.id, "never") }
            }
        }
    }
    ResizeSkeleton {
        id: resizeSkeleton
        objectName: "resizeSkeleton"
        anchors.fill: parent
        anchors.margins: 22
        paletteColors: panel.colors
        destination: panel.skeletonPage
        listMode: applications.listMode
        opacity: 0
        visible: opacity > 0
        // Keep pointer events off the obscured controls through the crossfade.
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
    }
    ActionToast {
        id: feedback
        objectName: "groupActionToast"
        paletteColors: panel.colors
        maximumWidth: panel.width - 44
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 54
        z: 10
    }
    NumberAnimation {
        id: revealAnimation
        target: resizeSkeleton
        property: "opacity"
        to: 0
        duration: 110
        easing.type: Easing.OutCubic
        onFinished: {
            panel.transitioning = false
            panel.focusCurrentSearch()
        }
    }
    Keys.onEscapePressed: {
        if (panel.transitioning) panel.backend.dismiss()
        else if (panel.downloadsExpanded) recentDownloads.escapeSearch()
        else if (panel.applicationsExpanded) applications.escapeSearch()
        else panel.escapeSearch()
    }
}
