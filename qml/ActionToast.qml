// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick
import org.kde.kirigami as Kirigami

Rectangle {
    id: toast
    required property var paletteColors
    property string message: ""
    property bool showing: false
    property real maximumWidth: 480
    function showMessage(text) {
        message = text
        showing = true
        lifetime.restart()
    }
    function clear() {
        lifetime.stop()
        showing = false
        message = ""
    }
    implicitWidth: Math.min(maximumWidth, label.implicitWidth + 58)
    implicitHeight: Math.max(42, label.implicitHeight + 20)
    width: implicitWidth
    height: implicitHeight
    radius: 8
    color: paletteColors.surface
    border.color: Qt.alpha(paletteColors.accent, 0.5)
    opacity: showing ? 1 : 0
    visible: message.length > 0 && opacity > 0
    Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    Timer { id: lifetime; interval: 1800; onTriggered: toast.showing = false }
    Kirigami.Icon {
        source: "dialog-positive"
        color: toast.paletteColors.accent
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 20
        height: 20
    }
    Text {
        id: label
        x: 42
        y: 10
        width: parent.width - x - 14
        text: toast.message
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: toast.paletteColors.text
        font.pixelSize: 13
    }
}
