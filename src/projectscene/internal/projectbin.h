/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "importexport/import/iimporter.h"
#include "interactive/iinteractive.h"
#include "modularity/ioc.h"
#include "playback/iplayback.h"
#include "project/iaudacityproject.h"
#include "trackedit/iclipsinteraction.h"
#include "trackedit/iprojecthistory.h"
#include "trackedit/iselectioncontroller.h"
#include "trackedit/itracksinteraction.h"

#include "projectscene/iprojectbin.h"

class TrackList;

namespace au::projectscene {
class ProjectBin : public IProjectBin, public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<importexport::IImporter> importer{ this };
    muse::ContextInject<muse::IInteractive> interactive{ this };
    muse::ContextInject<playback::IPlayback> playback{ this };
    muse::ContextInject<trackedit::IClipsInteraction> clipsInteraction{ this };
    muse::ContextInject<trackedit::ITracksInteraction> tracksInteraction{ this };
    muse::ContextInject<trackedit::ISelectionController> selectionController{ this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory{ this };

public:
    explicit ProjectBin(const muse::modularity::ContextPtr& ctx);

    void init();

    const std::vector<ProjectBinItem>& items() const override;
    muse::async::Notification itemsChanged() const override;

    void addFiles(const QStringList& fileUrls) override;
    bool moveClipToBin(const trackedit::ClipKey& clipKey) override;
    muse::Ret pasteItem(int index, const trackedit::TrackIdList& dstTrackIds, trackedit::secs_t startTime) override;
    bool previewItem(int index) override;
    bool renameItem(int index, const QString& title) override;
    bool removeItem(int index) override;
    int referenceCount(int index) const override;
    bool selectAllInstances(int index) override;
    bool isMissing(int index) const override;
    bool locateMissingReference(int index) override;

private:
    ProjectBinItem* itemAt(int index);
    const ProjectBinItem* itemAt(int index) const;

    muse::Ret pasteFileReference(ProjectBinItem& item, const trackedit::TrackIdList& dstTrackIds, trackedit::secs_t startTime);
    muse::Ret pasteTrackData(ProjectBinItem& item, const trackedit::TrackIdList& dstTrackIds, trackedit::secs_t startTime);

    std::vector<double> captureWaveform(const trackedit::ClipKey& clipKey) const;
    trackedit::ClipKeyList allProjectClipKeys() const;
    trackedit::ClipKeyList addedClipKeys(const trackedit::ClipKeyList& before) const;
    trackedit::ClipKeyList liveInstanceKeys(const ProjectBinItem& item) const;
    void addInstances(ProjectBinItem& item, const trackedit::ClipKeyList& before);

    bool previewTrackData(const ProjectBinItem& item);
    bool previewFileReference(const ProjectBinItem& item);
    bool playPreviewTracks(const std::shared_ptr<TrackList>& tracks, double endTime);
    void clearPreviewTracks();

    std::vector<ProjectBinItem> m_items;
    muse::async::Notification m_itemsChanged;
    std::shared_ptr<TrackList> m_previewTracks;
};
}
