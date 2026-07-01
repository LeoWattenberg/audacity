/*
 * Audacity: A Digital Audio Editor
 */
#include "projectbinmodel.h"

#include <algorithm>
#include <cmath>

#include "actions/actiontypes.h"
#include "log.h"
#include "types/translatablestring.h"
#include "ui/uitypes.h"

using namespace au::projectscene;
using namespace muse::uicomponents;

namespace {
constexpr int THUMBNAIL_VIEW_MODE = 0;
constexpr int COMPACT_VIEW_MODE = 1;
constexpr int LIST_VIEW_MODE = 2;

const QString THUMBNAIL_VIEW_MODE_ITEM_ID("projectbin-view-thumbnail");
const QString COMPACT_VIEW_MODE_ITEM_ID("projectbin-view-compact");
const QString LIST_VIEW_MODE_ITEM_ID("projectbin-view-list");
}

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
    case PreviewImageRole:
        return item->previewImage;
    case HasPreviewImageRole:
        return !item->previewImage.isEmpty();
    case ReferenceCountRole:
        return projectBin()->referenceCount(index.row());
    case MissingRole:
        return projectBin()->isMissing(index.row());
    case PreviewingRole:
        return projectBin()->previewingIndex() == index.row();
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
        { PreviewImageRole, "previewImage" },
        { HasPreviewImageRole, "hasPreviewImage" },
        { ReferenceCountRole, "referenceCount" },
        { MissingRole, "missing" },
        { PreviewingRole, "previewing" },
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

    projectBin()->previewStateChanged().onNotify(this, [this] {
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

bool ProjectBinModel::stopPreview()
{
    return projectBin()->stopPreview();
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

void ProjectBinModel::reload()
{
    beginResetModel();
    endResetModel();

    emit countChanged();
}

ProjectBinMenuModel::ProjectBinMenuModel(QObject* parent)
    : AbstractMenuModel(parent)
{
}

void ProjectBinMenuModel::init()
{
    if (m_inited) {
        return;
    }

    dispatcher()->reg(this, muse::actions::codeFromQString(THUMBNAIL_VIEW_MODE_ITEM_ID), [this]() {
        setViewMode(THUMBNAIL_VIEW_MODE);
    });
    dispatcher()->reg(this, muse::actions::codeFromQString(COMPACT_VIEW_MODE_ITEM_ID), [this]() {
        setViewMode(COMPACT_VIEW_MODE);
    });
    dispatcher()->reg(this, muse::actions::codeFromQString(LIST_VIEW_MODE_ITEM_ID), [this]() {
        setViewMode(LIST_VIEW_MODE);
    });

    m_inited = true;
}

int ProjectBinMenuModel::viewMode() const
{
    return m_viewMode;
}

void ProjectBinMenuModel::setViewMode(int viewMode)
{
    if (m_viewMode == viewMode) {
        return;
    }

    m_viewMode = viewMode;
    emit viewModeChanged();
    load();
}

void ProjectBinMenuModel::load()
{
    MenuItemList items {
        makeViewModeItem(THUMBNAIL_VIEW_MODE_ITEM_ID, muse::TranslatableString("projectbin", "Thumbnail"), THUMBNAIL_VIEW_MODE),
        makeViewModeItem(COMPACT_VIEW_MODE_ITEM_ID, muse::TranslatableString("projectbin", "Compact"), COMPACT_VIEW_MODE),
        makeViewModeItem(LIST_VIEW_MODE_ITEM_ID, muse::TranslatableString("projectbin", "List"), LIST_VIEW_MODE)
    };

    setItems(items);
}

void ProjectBinMenuModel::handleMenuItem(const QString& itemId)
{
    if (itemId == THUMBNAIL_VIEW_MODE_ITEM_ID) {
        setViewMode(THUMBNAIL_VIEW_MODE);
        return;
    }

    if (itemId == COMPACT_VIEW_MODE_ITEM_ID) {
        setViewMode(COMPACT_VIEW_MODE);
        return;
    }

    if (itemId == LIST_VIEW_MODE_ITEM_ID) {
        setViewMode(LIST_VIEW_MODE);
        return;
    }

    AbstractMenuModel::handleMenuItem(itemId);
}

MenuItem* ProjectBinMenuModel::makeViewModeItem(const QString& itemId, const muse::TranslatableString& title, int viewMode)
{
    MenuItem* item = new MenuItem(this);
    item->setId(itemId);

    muse::ui::UiAction action;
    action.code = muse::actions::codeFromQString(itemId);
    action.title = title;
    item->setAction(action);
    item->setCheckable(true);

    muse::ui::UiActionState state;
    state.enabled = true;
    state.checked = m_viewMode == viewMode;
    item->setState(state);

    return item;
}
