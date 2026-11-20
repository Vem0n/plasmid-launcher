// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls.Basic as Controls
import org.kde.kirigami as Kirigami

Controls.AbstractButton {
    id: tile
    required property string label
    required property string iconName
    required property var paletteColors
    property bool compact: false
    implicitWidth: compact ? 176 : 88
    implicitHeight: compact ? 50 : 90
    hoverEnabled: true
    Accessible.name: label
    Accessible.role: Accessible.Button

    background: Rectangle {
        radius: 8
        color: tile.down ? Qt.alpha(tile.paletteColors.accent, 0.20)
                        : tile.hovered || tile.activeFocus ? Qt.alpha(tile.paletteColors.text, 0.08) : "transparent"
        border.width: tile.activeFocus ? 1 : 0
        border.color: tile.paletteColors.accent
        Behavior on color { ColorAnimation { duration: 100 } }
    }
    contentItem: Item {
        Kirigami.Icon {
            id: icon
            source: tile.iconName
            width: tile.compact ? 24 : 34
            height: width
            x: tile.compact ? 14 : (parent.width - width) / 2
            y: tile.compact ? (parent.height - height) / 2 : 12
        }
        Text {
            text: tile.label
            textFormat: Text.PlainText
            color: tile.paletteColors.text
            font.pixelSize: 12
            x: tile.compact ? 48 : 4
            y: tile.compact ? (parent.height - height) / 2 : 56
            width: parent.width - x - 4
            horizontalAlignment: tile.compact ? Text.AlignLeft : Text.AlignHCenter
            elide: Text.ElideRight
        }
    }
}
