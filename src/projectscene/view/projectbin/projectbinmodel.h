/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <QAbstractListModel>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "projectscene/iprojectbin.h"
#include "projectscene/types/projectscenetypes.h"

namespace au::projectscene {
class ProjectBinModel : public QAbstractListModel, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged FINAL)

    muse::ContextInject<IProjectBin> projectBin{ this };

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        PathRole,
        DurationRole,
        DurationTextRole,
        TrackCountRole,
        SourceTypeRole,
        WaveformRole,
        HasWaveformRole,
        ReferenceCountRole,
        MissingRole,
        PreviewingRole
    };

    explicit ProjectBinModel(QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void init();
    Q_INVOKABLE void addFiles(const QStringList& fileUrls);
    Q_INVOKABLE int itemTrackCount(int index) const;
    Q_INVOKABLE QVariantList itemDurations(int index) const;
    Q_INVOKABLE QVariantList itemTitles(int index) const;
    Q_INVOKABLE bool moveClipToBin(const ClipKey& clipKey);
    Q_INVOKABLE void pasteItem(int index, const QVariantList& trackIds, double startTime);
    Q_INVOKABLE bool previewItem(int index);
    Q_INVOKABLE bool stopPreview();
    Q_INVOKABLE bool renameItem(int index, const QString& title);
    Q_INVOKABLE bool removeItem(int index);
    Q_INVOKABLE bool selectAllInstances(int index);
    Q_INVOKABLE bool locateMissingReference(int index);

signals:
    void countChanged();

private:
    const ProjectBinItem* itemAt(int index) const;
    QString durationText(double duration) const;
    QVariantList waveformData(const ProjectBinItem& item) const;
    void reload();

    bool m_inited = false;
};
}
