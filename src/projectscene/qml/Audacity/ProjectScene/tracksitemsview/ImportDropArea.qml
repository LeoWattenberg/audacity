import QtQuick
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents

import Audacity.ProjectScene
import Audacity.Project
import Audacity.Playback

DropArea {
    id: root

    property var tracksItemsView: null
    property var tracksViewState: null
    property var timeline: null

    signal setGuidelineRequested(var pos, bool visibility)

    signal externalDropAreaEntered(var drop)
    signal externalDropAreaExited
    signal externalDropAreaDropped(var drop)

    onExternalDropAreaEntered: drop => {
        handleOnEntered(drop, true)
    }

    onExternalDropAreaExited: {
        clearPreviewClipsTimer.start()
    }

    onExternalDropAreaDropped: drop => {
        handleOnDropped(drop, true)
    }

    QtObject {
        id: prv

        property var lastProbedUrls: null
        property int lastProjectBinIndex: -1
    }

    DropController {
        id: dropController
    }

    ProjectBinModel {
        id: projectBinModel

        Component.onCompleted: init()
    }

    Timer {
        id: clearPreviewClipsTimer
        interval: 100
        onTriggered: {
            tracksItemsView.clearPreviewImportClip([])
            prv.lastProbedUrls = null
            prv.lastProjectBinIndex = -1
            dropController.endImportDrag()
            root.setGuidelineRequested(-1, false)
        }
    }

    onEntered: drop => {
        handleOnEntered(drop, false)
    }

    onExited: {
        clearPreviewClipsTimer.start()
    }

    onPositionChanged:
    // NOTE! Qt does not reliably send onPositionChanged for external drags
    // it is expected that Qt may trigger entered/exited signals alternately
    // instead of positionChanged
    {}

    onDropped: drop => {
        handleOnDropped(drop, false)
    }

    function handleOnEntered(drop, externalDrop) {
        clearPreviewClipsTimer.stop()

        let projectBinIndex = projectBinDropIndex(drop)
        dropController.startImportDrag()

        if (projectBinIndex >= 0) {
            prv.lastProjectBinIndex = projectBinIndex
            prv.lastProbedUrls = null
        } else if (!prv.lastProbedUrls) {
            // NOTE: working with urls list from DropArea
            // is expensive so avoid it otherwise the preview clip
            // move will be laggy
            let urls = drop.urls
            if (urls) {
                dropController.probeAudioFiles(urls)
                prv.lastProbedUrls = urls
            }
            prv.lastProjectBinIndex = -1
        }

        let dropX = 0
        let dropY = drop.y
        if (!externalDrop) {
            dropX = drop.x
            dropY -= root.timeline.height
        }

        var trackId = tracksViewState.trackAtPosition(dropX, dropY)
        let trackCount = projectBinIndex >= 0 ? projectBinModel.itemTrackCount(projectBinIndex) : dropController.requiredTracksCount()
        dropController.prepareConditionalTracks(trackId, trackCount)
        dropController.removeDragAddedTracks(trackId, trackCount)

        let tracksIds = dropController.draggedTracksIds(trackId, trackCount)
        tracksItemsView.clearPreviewImportClip(tracksIds /* tracks not to clear */)
        const durations = projectBinIndex >= 0 ? projectBinModel.itemDurations(projectBinIndex) : dropController.lastProbedDurations()
        const titles = projectBinIndex >= 0 ? projectBinModel.itemTitles(projectBinIndex) : dropController.lastProbedFileNames()

        tracksItemsView.previewImportClipRequested(tracksIds, dropX, durations, titles)

        root.setGuidelineRequested(dropX, true)
    }

    function handleOnDropped(drop, externalDrop) {
        let dropX = 0
        let dropY = drop.y
        if (!externalDrop) {
            dropX = drop.x
            dropY -= root.timeline.height
        }

        let trackId = tracksViewState.trackAtPosition(dropX, dropY)
        let projectBinIndex = projectBinDropIndex(drop)
        if (projectBinIndex < 0) {
            projectBinIndex = prv.lastProjectBinIndex
        }
        let trackCount = projectBinIndex >= 0 ? projectBinModel.itemTrackCount(projectBinIndex) : dropController.requiredTracksCount()
        let tracksIds = dropController.draggedTracksIds(trackId, trackCount);
        if (projectBinIndex >= 0) {
            projectBinModel.pasteItem(projectBinIndex, tracksIds, timeline.context.positionToTime(dropX))
        } else {
            // by this time, url list is already inside dropController
            dropController.handleDroppedFiles(tracksIds, timeline.context.positionToTime(dropX))
        }

        dropController.endImportDrag()
        drop.acceptProposedAction()

        root.setGuidelineRequested(-1, false)
        prv.lastProbedUrls = null
        prv.lastProjectBinIndex = -1
    }

    function projectBinDropIndex(drop) {
        if (!drop || !drop.source || !drop.source.projectBinDrag) {
            return -1
        }

        return drop.source.projectBinIndex
    }
}
