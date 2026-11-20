// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as Controls

Controls.Button {
    id: button
    required property var paletteColors
    implicitHeight: 28
    leftPadding: 10
    rightPadding: 10
    contentItem: Text {
        text: button.text
        textFormat: Text.PlainText
        color: button.enabled ? button.paletteColors.text : button.paletteColors.muted
        font.pixelSize: 12
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 5
        color: button.down ? Qt.alpha(button.paletteColors.accent, 0.24)
            : button.hovered || button.activeFocus ? Qt.alpha(button.paletteColors.accent, 0.14)
            : button.flat ? "transparent" : button.paletteColors.surface
        border.width: button.activeFocus || !button.flat ? 1 : 0
        border.color: button.activeFocus ? button.paletteColors.accent : Qt.alpha(button.paletteColors.text, 0.14)
    }
}
