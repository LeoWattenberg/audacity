import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.ProjectScene

Item {
    id: root

    enum ViewMode {
        Thumbnail,
        Compact,
        List
    }

    property alias navigationSection: navPanel.section
    property alias navigationOrderStart: navPanel.order

    property int viewMode: ProjectBinPanel.Thumbnail

    NavigationPanel {
        id: navPanel
        name: "ProjectBinPanel"
        direction: NavigationPanel.Vertical
        enabled: root.enabled && root.visible
    }

    ProjectBinModel {
        id: projectBinModel

        Component.onCompleted: init()
    }

    function containsScenePoint(scenePoint) {
        let localPoint = root.mapFromItem(null, scenePoint.x, scenePoint.y)
        return root.visible && root.enabled && localPoint.x >= 0 && localPoint.x <= root.width && localPoint.y >= 0 && localPoint.y <= root.height
    }

    function moveTimelineClipToBin(clipKey) {
        return projectBinModel.moveClipToBin(clipKey)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42

            color: ui.theme.backgroundPrimaryColor
            border.width: ui.theme.borderWidth
            border.color: ui.theme.strokeColor

            RadioButtonGroup {
                id: viewModeButtons

                anchors.fill: parent
                anchors.margins: 6

                model: [
                    {
                        "title": qsTrc("projectbin", "Thumbnail"),
                        "mode": ProjectBinPanel.Thumbnail
                    },
                    {
                        "title": qsTrc("projectbin", "Compact"),
                        "mode": ProjectBinPanel.Compact
                    },
                    {
                        "title": qsTrc("projectbin", "List"),
                        "mode": ProjectBinPanel.List
                    }
                ]

                delegate: FlatRadioButton {
                    text: modelData.title
                    checked: root.viewMode === modelData.mode
                    navigation.panel: navPanel
                    navigation.order: index
                    onClicked: {
                        root.viewMode = modelData.mode
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            StyledListView {
                id: listView

                anchors.fill: parent
                anchors.margins: 8

                model: projectBinModel
                spacing: root.viewMode === ProjectBinPanel.Thumbnail ? 10 : 4
                clip: true
                scrollBarPolicy: ScrollBar.AsNeeded

                delegate: Item {
                    id: delegateRoot

                    readonly property int binIndex: model.index
                    readonly property bool thumbnailMode: root.viewMode === ProjectBinPanel.Thumbnail
                    readonly property bool compactMode: root.viewMode === ProjectBinPanel.Compact
                    readonly property bool listMode: root.viewMode === ProjectBinPanel.List
                    readonly property int leftColumnWidth: 34
                    readonly property int horizontalPadding: 8
                    readonly property int clipWidth: Math.max(1, width - leftColumnWidth - (2 * horizontalPadding))
                    readonly property int clipHeight: thumbnailMode ? Math.round(clipWidth * 9 / 16) : compactMode ? 32 : 28

                    width: ListView.view ? ListView.view.width : 240
                    height: thumbnailMode ? clipHeight + 10 : compactMode ? 42 : 34

                    Item {
                        id: dragSource

                        property bool projectBinDrag: true
                        property int projectBinIndex: delegateRoot.binIndex

                        x: clipSurface.x
                        y: clipSurface.y
                        width: clipSurface.width
                        height: clipSurface.height
                        opacity: 0

                        Drag.active: dragArea.drag.active
                        Drag.supportedActions: Qt.CopyAction
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: height / 2
                    }

                    FlatButton {
                        id: playButton

                        anchors.left: parent.left
                        anchors.leftMargin: delegateRoot.horizontalPadding
                        anchors.verticalCenter: parent.verticalCenter

                        width: 28
                        height: 28
                        minWidth: 28
                        margins: 0
                        icon: IconCode.PLAY
                        buttonType: FlatButton.IconOnly
                        toolTipTitle: qsTrc("projectbin", "Preview")

                        navigation.panel: navPanel
                        navigation.order: delegateRoot.binIndex * 4

                        onClicked: {
                            projectBinModel.previewItem(delegateRoot.binIndex)
                        }
                    }

                    Rectangle {
                        id: clipSurface

                        anchors.left: parent.left
                        anchors.leftMargin: delegateRoot.leftColumnWidth + delegateRoot.horizontalPadding
                        anchors.verticalCenter: parent.verticalCenter

                        width: delegateRoot.clipWidth
                        height: delegateRoot.clipHeight
                        radius: 4

                        color: delegateRoot.listMode ? "transparent" : ui.theme.backgroundPrimaryColor
                        border.width: delegateRoot.listMode ? 0 : 1
                        border.color: ui.theme.strokeColor

                        NavigationFocusBorder {
                            navigationCtrl: dragNav
                            visible: !delegateRoot.listMode
                        }

                        NavigationControl {
                            id: dragNav

                            name: "ProjectBinItem_" + delegateRoot.binIndex
                            enabled: root.enabled && root.visible
                            panel: navPanel
                            order: delegateRoot.binIndex * 4 + 1

                            accessible.role: MUAccessible.ListItem
                            accessible.name: model.title
                        }

                        Rectangle {
                            id: header

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top

                            height: delegateRoot.listMode ? 0 : delegateRoot.thumbnailMode ? 24 : 18
                            visible: !delegateRoot.listMode
                            radius: 4
                            color: ui.theme.backgroundSecondaryColor

                            StyledTextLabel {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                horizontalAlignment: Text.AlignLeft
                                maximumLineCount: 1
                                text: model.title
                                font: delegateRoot.thumbnailMode ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                            }
                        }

                        Row {
                            id: waveform

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: header.visible ? header.bottom : parent.top
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: delegateRoot.listMode ? 0 : 8
                            anchors.rightMargin: delegateRoot.listMode ? 0 : 8

                            visible: !delegateRoot.listMode
                            spacing: Math.max(2, Math.floor(width / 38))

                            Repeater {
                                model: 18

                                Rectangle {
                                    width: Math.max(2, Math.floor((waveform.width - (waveform.spacing * 17)) / 18))
                                    height: Math.max(4, waveform.height * (0.24 + ((index * 7) % 11) / 18))
                                    anchors.verticalCenter: parent.verticalCenter
                                    radius: 1
                                    color: ui.colorWithAlphaF(ui.theme.fontPrimaryColor, 0.38)
                                }
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 0
                            anchors.rightMargin: 0
                            visible: delegateRoot.listMode
                            spacing: 6

                            StyledTextLabel {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                maximumLineCount: 1
                                text: model.title
                            }

                            StyledTextLabel {
                                text: model.durationText
                                opacity: 0.7
                            }
                        }

                        StyledTextLabel {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 4
                            visible: !delegateRoot.listMode
                            text: model.durationText
                            opacity: 0.75
                        }

                        MouseArea {
                            id: dragArea

                            anchors.fill: parent
                            enabled: root.enabled
                            hoverEnabled: true
                            drag.target: dragSource

                            onPressed: {
                                dragNav.requestActiveByInteraction()
                                dragSource.x = clipSurface.x
                                dragSource.y = clipSurface.y
                            }

                            onReleased: {
                                dragSource.Drag.drop()
                                dragSource.x = clipSurface.x
                                dragSource.y = clipSurface.y
                            }
                        }
                    }
                }
            }

            StyledTextLabel {
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 220)
                horizontalAlignment: Text.AlignHCenter
                maximumLineCount: 2
                text: qsTrc("projectbin", "Drop audio files here")
                visible: projectBinModel.count === 0
                opacity: 0.7
            }

            DropArea {
                id: fileDropArea

                anchors.fill: parent

                onEntered: function (drop) {
                    if (drop.urls && drop.urls.length > 0) {
                        drop.acceptProposedAction()
                    }
                }

                onDropped: function (drop) {
                    if (drop.urls && drop.urls.length > 0) {
                        projectBinModel.addFiles(drop.urls)
                        drop.acceptProposedAction()
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: fileDropArea.containsDrag
                color: ui.colorWithAlphaF(ui.theme.accentColor, 0.12)
                border.width: 1
                border.color: ui.theme.accentColor
            }
        }
    }
}
