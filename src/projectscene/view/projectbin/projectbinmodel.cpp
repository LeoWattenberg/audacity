/*
 * Audacity: A Digital Audio Editor
 */
#include "projectbinmodel.h"

#include <algorithm>
#include <cmath>

#include "log.h"

using namespace au::projectscene;

ProjectBinModel::ProjectBinModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariant ProjectBinModel::data(const QModelIndex& index, int role) const
{
    const ProjectBinItem* item = itemAt(index.row());
    if (!item) {
        return {};
    }

    switch (role) {
    case TitleRole:
        return item->title;
    case PathRole:
        return item->path.toQString();
    case DurationRole:
        return item->duration;
    case DurationTextRole:
        return durationText(item->duration);
    case TrackCountRole:
        return item->trackCount;
    case SourceTypeRole:
        return static_cast<int>(item->sourceType);
    case WaveformRole:
        return waveformData(*item);
    case HasWaveformRole:
        return !item->waveform.empty();
    case ReferenceCountRole:
        return projectBin()->referenceCount(index.row());
    case MissingRole:
        return projectBin()->isMissing(index.row());
    default:
        return {};
    }
}

int ProjectBinModel::rowCount(const QModelIndex&) const
{
    return static_cast<int>(projectBin()->items().size());
}

QHash<int, QByteArray> ProjectBinModel::roleNames() const
{
    return {
        { TitleRole, "title" },
        { PathRole, "path" },
        { DurationRole, "duration" },
        { DurationTextRole, "durationText" },
        { TrackCountRole, "trackCount" },
        { SourceTypeRole, "sourceType" },
        { WaveformRole, "waveform" },
        { HasWaveformRole, "hasWaveform" },
        { ReferenceCountRole, "referenceCount" },
        { MissingRole, "missing" },
    };
}

void ProjectBinModel::init()
{
    if (m_inited) {
        return;
    }

    projectBin()->itemsChanged().onNotify(this, [this] {
        reload();
    });

    m_inited = true;
}

void ProjectBinModel::addFiles(const QStringList& fileUrls)
{
    projectBin()->addFiles(fileUrls);
}

int ProjectBinModel::itemTrackCount(int index) const
{
    const ProjectBinItem* item = itemAt(index);
    if (!item) {
        return 0;
    }

    return item->trackCount;
}

QVariantList ProjectBinModel::itemDurations(int index) const
{
    QVariantList durations;

    const ProjectBinItem* item = itemAt(index);
    if (!item) {
        return durations;
    }

    durations.reserve(item->trackCount);
    for (int i = 0; i < item->trackCount; ++i) {
        durations.push_back(item->duration);
    }

    return durations;
}

QVariantList ProjectBinModel::itemTitles(int index) const
{
    QVariantList titles;

    const ProjectBinItem* item = itemAt(index);
    if (!item) {
        return titles;
    }

    titles.reserve(item->trackCount);
    for (int i = 0; i < item->trackCount; ++i) {
        titles.push_back(item->title);
    }

    return titles;
}

bool ProjectBinModel::moveClipToBin(const ClipKey& clipKey)
{
    return projectBin()->moveClipToBin(clipKey.key);
}

void ProjectBinModel::pasteItem(int index, const QVariantList& trackIds, double startTime)
{
    trackedit::TrackIdList dstTrackIds;
    dstTrackIds.reserve(static_cast<size_t>(trackIds.size()));

    for (const QVariant& trackId : trackIds) {
        dstTrackIds.push_back(trackId.toLongLong());
    }

    const muse::Ret ret = projectBin()->pasteItem(index, dstTrackIds, startTime);
    if (!ret) {
        LOGE() << ret.toString();
    }
}

bool ProjectBinModel::previewItem(int index)
{
    return projectBin()->previewItem(index);
}

bool ProjectBinModel::renameItem(int index, const QString& title)
{
    return projectBin()->renameItem(index, title);
}

bool ProjectBinModel::removeItem(int index)
{
    return projectBin()->removeItem(index);
}

bool ProjectBinModel::selectAllInstances(int index)
{
    return projectBin()->selectAllInstances(index);
}

bool ProjectBinModel::locateMissingReference(int index)
{
    return projectBin()->locateMissingReference(index);
}

const ProjectBinItem* ProjectBinModel::itemAt(int index) const
{
    const std::vector<ProjectBinItem>& items = projectBin()->items();
    if (index < 0 || index >= static_cast<int>(items.size())) {
        return nullptr;
    }

    return &items[static_cast<size_t>(index)];
}

QString ProjectBinModel::durationText(double duration) const
{
    const int totalSeconds = std::max(0, static_cast<int>(std::round(duration)));
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int seconds = totalSeconds % 60;

    if (hours > 0) {
        return QString::number(hours) + QStringLiteral(":")
               + QStringLiteral("%1").arg(minutes, 2, 10, QLatin1Char('0')) + QStringLiteral(":")
               + QStringLiteral("%1").arg(seconds, 2, 10, QLatin1Char('0'));
    }

    return QString::number(minutes) + QStringLiteral(":")
           + QStringLiteral("%1").arg(seconds, 2, 10, QLatin1Char('0'));
}

QVariantList ProjectBinModel::waveformData(const ProjectBinItem& item) const
{
    QVariantList values;
    values.reserve(static_cast<int>(item.waveform.size()));

    for (double value : item.waveform) {
        values.push_back(value);
    }

    return values;
}

void ProjectBinModel::reload()
{
    beginResetModel();
    endResetModel();

    emit countChanged();
}
