// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic as Controls
import org.kde.layershell as LayerShell

Window {
    id: window
    required property var backend
    property real surfacePanelWidth: 616
    width: surfacePanelWidth + 24
    property real surfacePanelHeight: 412
    height: surfacePanelHeight + 36
    Component.onCompleted: {
        surfacePanelHeight = panel.implicitHeight
        surfacePanelWidth = panel.implicitWidth
    }
    function fitSurface() {
        if (!panel.resizing) {
            // Reuse the height allocation for inline groups. Shrinking the
            // native surface after the visual collapse caused a final flash.
            if (panel.expanded || panel.returningToMain || panel.transitioning)
                surfacePanelHeight = panel.implicitHeight
            surfacePanelWidth = panel.implicitWidth
        }
    }
    function updateInputArea() {
        backend.setInputArea(Qt.rect(panel.x, panel.y, panel.width, panel.height), window.Controls.Overlay.overlay)
    }
    visible: false
    color: "transparent"
    title: "Plasmid"
    flags: Qt.FramelessWindowHint | Qt.Tool

    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop
    LayerShell.Window.exclusionZone: 0
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityOnDemand
    LayerShell.Window.activateOnShow: true
    LayerShell.Window.wantsToBeOnActiveScreen: true
    LayerShell.Window.scope: "plasmid"

    property bool dismissing: false
    property bool hadFocus: false
    onActiveChanged: {
        if (active) hadFocus = true
        else if (visible && hadFocus && !dismissing) backend.dismiss()
    }
    onClosing: event => {
        event.accepted = false
        backend.dismiss()
    }

    Connections {
        target: window.backend
        function onOpening() {
            hideTimer.stop()
            window.dismissing = false
            window.hadFocus = false
            panel.reset()
            window.surfacePanelHeight = panel.implicitHeight
            window.surfacePanelWidth = panel.implicitWidth
            window.visible = true
            window.requestActivate()
        }
        function onClosing() {
            if (!window.visible || window.dismissing) return
            window.dismissing = true
            hideTimer.start()
        }
        function onExternalClosing() {
            hideTimer.stop()
            window.dismissing = true
            window.visible = false
            if (!window.backend.searchExecuting) panel.clearQuery()
        }
        function onSearchFinished() { panel.clearQuery() }
        function onSearchResuming() {
            hideTimer.stop()
            window.dismissing = false
            window.hadFocus = false
            window.visible = true
            window.requestActivate()
            panel.focusSearch()
        }
    }
    Timer {
        id: hideTimer
        interval: 130
        onTriggered: {
            window.visible = false
            panel.clearQuery()
        }
    }

    Rectangle {
        anchors.fill: panel
        anchors.margins: -5
        anchors.topMargin: -2
        anchors.bottomMargin: -9
        radius: 17
        color: "#24000000"
        opacity: panel.opacity
    }
    LauncherPanel {
        id: panel
        backend: window.backend
        maximumHeight: Math.max(260, Screen.desktopAvailableHeight - 60)
        maximumWidth: Math.max(260, Screen.desktopAvailableWidth - 72)
        // Grow before expansion. Navigation can shrink under its skeleton;
        // inline groups keep the allocation and only change their input area.
        onImplicitHeightChanged: window.surfacePanelHeight = Math.max(window.surfacePanelHeight, implicitHeight)
        onImplicitWidthChanged: window.surfacePanelWidth = Math.max(window.surfacePanelWidth, implicitWidth)
        onXChanged: window.updateInputArea()
        onYChanged: window.updateInputArea()
        onWidthChanged: window.updateInputArea()
        onHeightChanged: window.updateInputArea()
        Component.onCompleted: window.updateInputArea()
        onResizingChanged: {
            // Width and height Behaviors may change state in either order.
            // Commit the final surface size once both have settled.
            if (!resizing) Qt.callLater(window.fitSurface)
        }
        x: (window.width - width) / 2
        y: window.dismissing ? 0 : 16
        opacity: window.dismissing ? 0 : 1
        Behavior on y { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }
}
