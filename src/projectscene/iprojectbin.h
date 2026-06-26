/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <vector>

#include <QString>
#include <QStringList>

#include "async/notification.h"
#include "global/io/path.h"
#include "global/modularity/imoduleinterface.h"
#include "global/types/ret.h"
#include "trackedit/itrackdata.h"
#include "trackedit/trackedittypes.h"

namespace au::projectscene {
struct ProjectBinItem
{
    enum class SourceType {
        FileReference,
        TrackData
    };

    SourceType sourceType = SourceType::FileReference;
    QString title;
    muse::io::path_t path;
    double duration = 0.0;
    int trackCount = 1;
    std::vector<double> waveform;
    trackedit::ClipKeyList instanceKeys;
    std::vector<trackedit::ITrackDataPtr> trackData;
};

class IProjectBin : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IProjectBin)

public:
    virtual ~IProjectBin() = default;

    virtual const std::vector<ProjectBinItem>& items() const = 0;
    virtual muse::async::Notification itemsChanged() const = 0;

    virtual void addFiles(const QStringList& fileUrls) = 0;
    virtual bool moveClipToBin(const trackedit::ClipKey& clipKey) = 0;
    virtual muse::Ret pasteItem(int index, const trackedit::TrackIdList& dstTrackIds, trackedit::secs_t startTime) = 0;
    virtual bool previewItem(int index) = 0;
    virtual bool renameItem(int index, const QString& title) = 0;
    virtual bool removeItem(int index) = 0;
    virtual int referenceCount(int index) const = 0;
    virtual bool selectAllInstances(int index) = 0;
    virtual bool isMissing(int index) const = 0;
    virtual bool locateMissingReference(int index) = 0;
};
}
