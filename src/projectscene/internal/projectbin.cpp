/*
 * Audacity: A Digital Audio Editor
 */
#include "projectbin.h"

#include <algorithm>

#include <QUrl>

#include "global/containers.h"
#include "global/io/path.h"
#include "trackedit/dom/clip.h"

#include "log.h"

using namespace au::projectscene;

ProjectBin::ProjectBin(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx)
{
}

void ProjectBin::init()
{
    globalContext()->currentProjectChanged().onNotify(this, [this] {
        if (m_items.empty()) {
            return;
        }

        m_items.clear();
        m_itemsChanged.notify();
    });
}

const std::vector<ProjectBinItem>& ProjectBin::items() const
{
    return m_items;
}

muse::async::Notification ProjectBin::itemsChanged() const
{
    return m_itemsChanged;
}

void ProjectBin::addFiles(const QStringList& fileUrls)
{
    const auto supportedExtensions = importer()->supportedExtensions();

    bool changed = false;
    for (const QString& fileUrl : fileUrls) {
        const QUrl url(fileUrl);
        const QString local = url.isLocalFile() ? url.toLocalFile() : fileUrl;
        const muse::io::path_t path(local);

        if (!muse::contains(supportedExtensions, muse::io::suffix(path))) {
            continue;
        }

        au::importexport::FileInfo fileInfo = importer()->fileInfo(path);
        if (fileInfo.isEmpty()) {
            continue;
        }

        ProjectBinItem item;
        item.sourceType = ProjectBinItem::SourceType::FileReference;
        item.title = muse::io::filename(path, false /* includingExtension */).toQString();
        item.path = fileInfo.path;
        item.duration = fileInfo.duration;
        item.trackCount = std::max(1, fileInfo.trackCount);

        m_items.push_back(std::move(item));
        changed = true;
    }

    if (changed) {
        m_itemsChanged.notify();
    }
}

bool ProjectBin::moveClipToBin(const trackedit::ClipKey& clipKey)
{
    const trackedit::ITrackeditProjectPtr project = globalContext()->currentTrackeditProject();
    if (!project || !clipKey.isValid()) {
        return false;
    }

    const trackedit::Clip clip = project->clip(clipKey);
    if (!clip.isValid()) {
        return false;
    }

    trackedit::ITrackDataPtr data = clipsInteraction()->cutClip(clipKey);
    if (!data) {
        return false;
    }

    ProjectBinItem item;
    item.sourceType = ProjectBinItem::SourceType::TrackData;
    item.title = clip.title.empty() ? QString::fromStdString("Clip") : clip.title.toQString();
    item.duration = std::max(0.0, clip.endTime - clip.startTime);
    item.trackCount = 1;
    item.trackData.push_back(std::move(data));

    m_items.push_back(std::move(item));
    m_itemsChanged.notify();

    projectHistory()->pushHistoryState("Moved clip to project bin", "Project Bin");
    return true;
}

muse::Ret ProjectBin::pasteItem(int index, const trackedit::TrackIdList& dstTrackIds, trackedit::secs_t startTime)
{
    const ProjectBinItem* item = itemAt(index);
    if (!item) {
        return muse::make_ret(muse::Ret::Code::UnknownError);
    }

    switch (item->sourceType) {
    case ProjectBinItem::SourceType::FileReference:
        return pasteFileReference(*item, dstTrackIds, startTime);
    case ProjectBinItem::SourceType::TrackData:
        return pasteTrackData(*item, dstTrackIds, startTime);
    }

    return muse::make_ret(muse::Ret::Code::UnknownError);
}

bool ProjectBin::previewItem(int index)
{
    const ProjectBinItem* item = itemAt(index);
    if (!item) {
        return false;
    }

    if (item->sourceType != ProjectBinItem::SourceType::FileReference || item->path.empty()) {
        LOGW() << "Preview is only available for project bin file references";
        return false;
    }

    const muse::Ret ret = platformInteractive()->openUrl(QUrl::fromLocalFile(item->path.toQString()));
    if (!ret) {
        LOGE() << ret.toString();
        return false;
    }

    return true;
}

ProjectBinItem* ProjectBin::itemAt(int index)
{
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return nullptr;
    }

    return &m_items[static_cast<size_t>(index)];
}

const ProjectBinItem* ProjectBin::itemAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return nullptr;
    }

    return &m_items[static_cast<size_t>(index)];
}

muse::Ret ProjectBin::pasteFileReference(const ProjectBinItem& item, const trackedit::TrackIdList& dstTrackIds,
                                         trackedit::secs_t startTime)
{
    if (item.path.empty() || dstTrackIds.empty()) {
        return muse::make_ret(muse::Ret::Code::UnknownError);
    }

    const project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return muse::make_ret(muse::Ret::Code::UnknownError);
    }

    return project->importIntoTracks({ item.path }, { dstTrackIds.front() }, startTime);
}

muse::Ret ProjectBin::pasteTrackData(const ProjectBinItem& item, const trackedit::TrackIdList& dstTrackIds,
                                     trackedit::secs_t startTime)
{
    if (item.trackData.empty() || dstTrackIds.empty()) {
        return muse::make_ret(muse::Ret::Code::UnknownError);
    }

    selectionController()->setSelectedTracks(dstTrackIds);

    bool modified = false;
    muse::Ret ret = tracksInteraction()->paste(item.trackData, startTime, false /* moveClips */, false /* moveAllTracks */,
                                               false /* isMultiSelectionCopy */, modified);
    if (!ret) {
        return ret;
    }

    if (modified) {
        projectHistory()->pushHistoryState("Dropped project bin item", "Project Bin");
    }

    return ret;
}
