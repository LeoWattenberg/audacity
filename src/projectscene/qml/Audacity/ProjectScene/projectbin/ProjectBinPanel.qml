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

    function isTimelineClipDrop(drop) {
        return drop && drop.source && drop.source.timelineClipDrag && drop.source.timelineClipKey
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
                    readonly property bool hasPreviewImage: model.hasPreviewImage
                    readonly property string previewImage: model.previewImage
                    readonly property int leftColumnWidth: 34
                    readonly property int horizontalPadding: 8
                    readonly property int actionsWidth: 108
                    readonly property int clipWidth: Math.max(1, width - leftColumnWidth - (2 * horizontalPadding))
                    readonly property int clipHeight: thumbnailMode ? Math.round(clipWidth * 9 / 16) : compactMode ? 32 : 28
                    property bool editingTitle: false

                    function beginRename() {
                        editingTitle = true
                        renameInput.currentText = model.title
                        listRenameInput.currentText = model.title
                        let activeInput = listMode ? listRenameInput : renameInput
                        activeInput.ensureActiveFocus()
                        activeInput.selectAll()
                    }

                    function finishRename() {
                        if (!editingTitle) {
                            return
                        }

                        editingTitle = false
                        projectBinModel.renameItem(binIndex, listMode ? listRenameInput.currentText : renameInput.currentText)
                    }

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
                        icon: model.previewing ? IconCode.STOP_FILL : IconCode.PLAY_FILL
                        buttonType: FlatButton.IconOnly
                        toolTipTitle: model.previewing ? qsTrc("projectbin", "Stop preview") : qsTrc("projectbin", "Preview")

                        navigation.panel: navPanel
                        navigation.order: delegateRoot.binIndex * 8

                        onClicked: {
                            if (model.previewing) {
                                projectBinModel.stopPreview()
                            } else {
                                projectBinModel.previewItem(delegateRoot.binIndex)
                            }
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
                        border.color: model.missing ? "orange" : ui.theme.strokeColor

                        NavigationFocusBorder {
                            navigationCtrl: dragNav
                            visible: !delegateRoot.listMode
                        }

                        NavigationControl {
                            id: dragNav

                            name: "ProjectBinItem_" + delegateRoot.binIndex
                            enabled: root.enabled && root.visible
                            panel: navPanel
                            order: delegateRoot.binIndex * 8 + 1

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
                                anchors.rightMargin: delegateRoot.actionsWidth
                                horizontalAlignment: Text.AlignLeft
                                maximumLineCount: 1
                                visible: !delegateRoot.editingTitle
                                text: model.title
                                font: delegateRoot.thumbnailMode ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                            }

                            TextInputField {
                                id: renameInput

                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: delegateRoot.actionsWidth
                                visible: delegateRoot.editingTitle
                                currentText: model.title
                                background.color: header.color
                                background.border.width: 0
                                background.radius: 0
                                textSidePadding: 2

                                onAccepted: {
                                    delegateRoot.finishRename()
                                }

                                onEscaped: {
                                    delegateRoot.editingTitle = false
                                }

                                onFocusChanged: {
                                    if (!focus && delegateRoot.editingTitle) {
                                        delegateRoot.finishRename()
                                    }
                                }
                            }
                        }

                        Image {
                            id: previewImage

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: header.visible ? header.bottom : parent.top
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: delegateRoot.listMode ? 0 : 1
                            anchors.rightMargin: delegateRoot.listMode ? 0 : 1
                            anchors.bottomMargin: delegateRoot.listMode ? 0 : 1

                            visible: !delegateRoot.listMode && delegateRoot.hasPreviewImage
                            source: delegateRoot.previewImage
                            fillMode: Image.Stretch
                            cache: false
                            smooth: false
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: header.visible ? header.bottom : parent.top
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: delegateRoot.listMode ? 0 : 8
                            anchors.rightMargin: delegateRoot.listMode ? 0 : 8

                            visible: !delegateRoot.listMode && !delegateRoot.hasPreviewImage
                            color: "transparent"

                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width
                                height: 1
                                color: ui.colorWithAlphaF(ui.theme.fontPrimaryColor, 0.24)
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 0
                            anchors.rightMargin: delegateRoot.actionsWidth
                            visible: delegateRoot.listMode
                            spacing: 6

                            StyledTextLabel {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                maximumLineCount: 1
                                visible: !delegateRoot.editingTitle
                                text: model.title
                            }

                            TextInputField {
                                id: listRenameInput

                                Layout.fillWidth: true
                                Layout.preferredHeight: 28
                                visible: delegateRoot.editingTitle
                                currentText: model.title
                                textSidePadding: 2

                                onAccepted: {
                                    delegateRoot.finishRename()
                                }

                                onEscaped: {
                                    delegateRoot.editingTitle = false
                                }

                                onFocusChanged: {
                                    if (!focus && delegateRoot.editingTitle) {
                                        delegateRoot.finishRename()
                                    }
                                }
                            }

                            StyledTextLabel {
                                text: model.durationText
                                opacity: 0.7
                            }
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 4
                            visible: !delegateRoot.listMode
                            spacing: 8

                            StyledTextLabel {
                                visible: model.referenceCount > 0
                                text: qsTrc("projectbin", "%n use(s)", "", model.referenceCount)
                                opacity: 0.75
                            }

                            StyledTextLabel {
                                text: model.durationText
                                opacity: 0.75
                            }
                        }

                        StyledTextLabel {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 4
                            visible: model.missing && !delegateRoot.listMode
                            text: qsTrc("projectbin", "Missing")
                            color: "orange"
                            opacity: 0.9
                        }

                        MouseArea {
                            id: dragArea

                            anchors.fill: parent
                            enabled: root.enabled && !delegateRoot.editingTitle
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

                        Row {
                            id: actionsRow

                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            z: 2

                            FlatButton {
                                width: 24
                                height: 24
                                minWidth: 24
                                margins: 0
                                icon: IconCode.EDIT
                                buttonType: FlatButton.IconOnly
                                toolTipTitle: qsTrc("projectbin", "Rename")
                                navigation.panel: navPanel
                                navigation.order: delegateRoot.binIndex * 8 + 2
                                onClicked: {
                                    delegateRoot.beginRename()
                                }
                            }

                            FlatButton {
                                width: 24
                                height: 24
                                minWidth: 24
                                margins: 0
                                icon: IconCode.WAVEFORM
                                buttonType: FlatButton.IconOnly
                                enabled: model.referenceCount > 0
                                toolTipTitle: qsTrc("projectbin", "Select all instances")
                                navigation.panel: navPanel
                                navigation.order: delegateRoot.binIndex * 8 + 3
                                onClicked: {
                                    projectBinModel.selectAllInstances(delegateRoot.binIndex)
                                }
                            }

                            FlatButton {
                                width: 24
                                height: 24
                                minWidth: 24
                                margins: 0
                                icon: IconCode.OPEN_FILE
                                buttonType: FlatButton.IconOnly
                                visible: model.missing
                                toolTipTitle: qsTrc("projectbin", "Locate")
                                navigation.panel: navPanel
                                navigation.order: delegateRoot.binIndex * 8 + 4
                                onClicked: {
                                    projectBinModel.locateMissingReference(delegateRoot.binIndex)
                                }
                            }

                            FlatButton {
                                width: 24
                                height: 24
                                minWidth: 24
                                margins: 0
                                icon: IconCode.DELETE_TANK
                                buttonType: FlatButton.IconOnly
                                toolTipTitle: qsTrc("projectbin", "Remove from project bin")
                                navigation.panel: navPanel
                                navigation.order: delegateRoot.binIndex * 8 + 5
                                onClicked: {
                                    projectBinModel.removeItem(delegateRoot.binIndex)
                                }
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
                    if (root.isTimelineClipDrop(drop)) {
                        drop.acceptProposedAction()
                        return
                    }

                    if (drop.urls && drop.urls.length > 0) {
                        drop.acceptProposedAction()
                    }
                }

                onDropped: function (drop) {
                    if (root.isTimelineClipDrop(drop)) {
                        drop.source.timelineClipDroppedOnProjectBin = true
                        drop.acceptProposedAction()
                        return
                    }

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
