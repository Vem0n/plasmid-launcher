// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

Item {
    id: skeleton
    required property var paletteColors
    required property string destination
    property bool listMode: false
    readonly property bool home: destination === "home"
    readonly property bool gallery: destination === "applications" && !listMode
    Accessible.ignored: true

    component Bone: Rectangle {
        radius: 5
        color: Qt.alpha(skeleton.paletteColors.text, 0.12)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14
        opacity: 0.65
        SequentialAnimation on opacity {
            running: skeleton.visible
            loops: Animation.Infinite
            NumberAnimation { to: 1; duration: 420; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.65; duration: 420; easing.type: Easing.InOutSine }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 10
            Bone { Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
            Bone { Layout.preferredWidth: skeleton.home ? 72 : 142; Layout.preferredHeight: 12 }
            Item { Layout.fillWidth: true }
            Bone { Layout.preferredWidth: 65; Layout.preferredHeight: 10 }
        }
        Bone {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            radius: 7
            color: Qt.alpha(skeleton.paletteColors.text, 0.06)
            border.color: Qt.alpha(skeleton.paletteColors.text, 0.08)
            Bone { x: 15; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; radius: 9 }
            Bone { x: 47; anchors.verticalCenter: parent.verticalCenter; width: Math.min(180, parent.width / 2); height: 10 }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            Column {
                width: parent.width
                spacing: 14
                visible: skeleton.home
                Bone { width: 48; height: 10 }
                Row {
                    spacing: 8
                    Repeater {
                        model: 5
                        Column {
                            width: (skeleton.width - 32) / 5
                            spacing: 9
                            Bone { anchors.horizontalCenter: parent.horizontalCenter; width: 34; height: 34; radius: 8 }
                            Bone { anchors.horizontalCenter: parent.horizontalCenter; width: parent.width * 0.65; height: 9 }
                        }
                    }
                }
                Bone { width: parent.width; height: 1 }
                Row {
                    spacing: 16
                    Repeater {
                        model: 3
                        Bone { width: (skeleton.width - 32) / 3; height: 34 }
                    }
                }
            }
            Grid {
                id: skeletonGrid
                width: parent.width
                visible: !skeleton.home
                columns: skeleton.gallery ? Math.max(1, Math.floor(width / 130)) : 1
                spacing: 12
                Repeater {
                    model: skeleton.gallery ? 18 : 8
                    Item {
                        width: (skeleton.width - (skeletonGrid.columns - 1) * 12) / skeletonGrid.columns
                        height: skeleton.gallery ? 90 : 46
                        Bone {
                            x: skeleton.gallery ? (parent.width - width) / 2 : 8
                            y: 6
                            width: skeleton.gallery ? 40 : 30
                            height: width
                            radius: 8
                        }
                        Bone {
                            x: skeleton.gallery ? (parent.width - width) / 2 : 52
                            y: skeleton.gallery ? 58 : 12
                            width: skeleton.gallery ? parent.width * 0.65 : parent.width * 0.45
                            height: 10
                        }
                    }
                }
            }
        }
        Bone { Layout.preferredWidth: 166; Layout.preferredHeight: 10 }
    }
}
