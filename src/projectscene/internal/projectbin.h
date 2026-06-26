/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "importexport/import/iimporter.h"
#include "interactive/iplatforminteractive.h"
#include "modularity/ioc.h"
#include "project/iaudacityproject.h"
#include "trackedit/iclipsinteraction.h"
#include "trackedit/iprojecthistory.h"
#include "trackedit/iselectioncontroller.h"
#include "trackedit/itracksinteraction.h"

#include "projectscene/iprojectbin.h"

namespace au::projectscene {
class ProjectBin : public IProjectBin, public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<importexport::IImporter> importer{ this };
    muse::ContextInject<trackedit::IClipsInteraction> clipsInteraction{ this };
    muse::ContextInject<trackedit::ITracksInteraction> tracksInteraction{ this };
    muse::ContextInject<trackedit::ISelectionController> selectionController{ this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory{ this };

    muse::GlobalInject<muse::IPlatformInteractive> platformInteractive;

public:
    explicit ProjectBin(const muse::modularity::ContextPtr& ctx);

    void init();

    const std::vector<ProjectBinItem>& items() const override;
    muse::async::Notification itemsChanged() const override;

    void addFiles(const QStringList& fileUrls) override;
    bool moveClipToBin(const trackedit::ClipKey& clipKey) override;
    muse::Ret pasteItem(int index, const trackedit::TrackIdList& dstTrackIds, trackedit::secs_t startTime) override;
    bool previewItem(int index) override;

private:
    ProjectBinItem* itemAt(int index);
    const ProjectBinItem* itemAt(int index) const;

    muse::Ret pasteFileReference(const ProjectBinItem& item, const trackedit::TrackIdList& dstTrackIds, trackedit::secs_t startTime);
    muse::Ret pasteTrackData(const ProjectBinItem& item, const trackedit::TrackIdList& dstTrackIds, trackedit::secs_t startTime);

    std::vector<ProjectBinItem> m_items;
    muse::async::Notification m_itemsChanged;
};
}
