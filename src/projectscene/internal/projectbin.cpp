/*
 * Audacity: A Digital Audio Editor
 */
#include "projectbin.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include "async/async.h"
#include "global/containers.h"
#include "global/io/path.h"
#include "trackedit/dom/clip.h"
#include "trackedit/internal/au3/au3trackdata.h"

#include "au3-audio-io/AudioIO.h"
#include "au3-audio-io/ProjectAudioIO.h"
#include "au3-file-formats/AcidizerTags.h"
#include "au3-import-export/Import.h"
#include "au3-mixer/AudioIOSequences.h"
#include "au3-mixer/Envelope.h"
#include "au3-project/Project.h"
#include "au3-stretching-sequence/StretchingSequence.h"
#include "au3-tags/Tags.h"
#include "au3-track/Track.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-wave-track/WaveClipUtilities.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3wrap/au3types.h"
#include "au3wrap/internal/domaccessor.h"
#include "log.h"

using namespace au::projectscene;
using namespace au::au3;

namespace {
constexpr int WAVEFORM_BUCKET_COUNT = 48;
constexpr int WAVEFORM_SAMPLES_PER_BUCKET = 8;

void selectTrackForPreview(const std::shared_ptr<Track>& track)
{
    if (!track) {
        return;
    }

    track->SetSelected(true);

    if (auto waveTrack = std::dynamic_pointer_cast<WaveTrack>(track)) {
        waveTrack->MoveTo(0.0);
        waveTrack->SetMute(false);
    }
}

bool accumulateWaveClipPeak(const WaveClip& waveClip, double time, double& peak)
{
    if (time < waveClip.Start() || time >= waveClip.End()) {
        return false;
    }

    const auto sampleOffset = waveClip.TimeToSamples(time);
    const double adjustedTime = waveClip.SamplesToTime(sampleOffset);
    if (adjustedTime < waveClip.Start() || adjustedTime >= waveClip.End()) {
        return false;
    }

    bool sampled = false;
    for (size_t channel = 0; channel < waveClip.NChannels(); ++channel) {
        float oneSample = 0.0f;
        if (!WaveClipUtilities::GetFloatAtTime(waveClip, adjustedTime, channel, oneSample, false)) {
            continue;
        }

        const double envelope = waveClip.GetEnvelope().GetValue(adjustedTime, 1.0 / waveClip.GetRate());
        peak = std::max(peak, std::abs(static_cast<double>(oneSample) * envelope));
        sampled = true;
    }

    return sampled;
}

TransportSequences makePreviewTransportTracks(TrackList& trackList)
{
    TransportSequences result;
    const auto range = trackList.Any<WaveTrack>() + &Track::Any;
    for (auto pTrack : range) {
        result.playbackSequences.push_back(
            StretchingSequence::Create(*pTrack, pTrack->GetClipInterfaces()));
    }

    return result;
}
}

ProjectBin::ProjectBin(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx)
{
}

ProjectBin::~ProjectBin()
{
    if (m_previewPollTimer) {
        m_previewPollTimer->stop();
    }

    const int token = m_previewToken;
    m_previewToken = 0;
    m_previewingIndex = -1;

    AudioIO* audioIO = AudioIO::Get();
    if (audioIO && token != 0 && audioIO->IsStreamActive(token)) {
        audioIO->StopStream();
    }

    m_previewTracks.reset();
}

void ProjectBin::init()
{
    if (!m_previewPollTimer) {
        m_previewPollTimer = std::make_unique<QTimer>();
        m_previewPollTimer->setInterval(100);
        QObject::connect(m_previewPollTimer.get(), &QTimer::timeout, m_previewPollTimer.get(), [this] {
            pollPreview();
        });
    }

    globalContext()->currentProjectChanged().onNotify(this, [this] {
        stopPreview();
        clearPreviewTracks();
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

muse::async::Notification ProjectBin::previewStateChanged() const
{
    return m_previewStateChanged;
}

int ProjectBin::previewingIndex() const
{
    return m_previewingIndex;
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
        item.waveform = captureFileWaveform(item.path, item.duration);

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

    std::vector<double> waveform = captureWaveform(clipKey);

    trackedit::ITrackDataPtr data = clipsInteraction()->cutClip(clipKey);
    if (!data) {
        return false;
    }

    ProjectBinItem item;
    item.sourceType = ProjectBinItem::SourceType::TrackData;
    item.title = clip.title.empty() ? QString::fromStdString("Clip") : clip.title.toQString();
    item.duration = std::max(0.0, clip.endTime - clip.startTime);
    item.trackCount = 1;
    item.waveform = std::move(waveform);
    item.trackData.push_back(std::move(data));

    m_items.push_back(std::move(item));
    m_itemsChanged.notify();

    projectHistory()->pushHistoryState("Moved clip to project bin", "Project Bin");
    return true;
}

muse::Ret ProjectBin::pasteItem(int index, const trackedit::TrackIdList& dstTrackIds, trackedit::secs_t startTime)
{
    ProjectBinItem* item = itemAt(index);
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

    if (m_previewingIndex == index) {
        return stopPreview();
    }

    stopPreview();

    switch (item->sourceType) {
    case ProjectBinItem::SourceType::FileReference:
        return previewFileReference(index, *item);
    case ProjectBinItem::SourceType::TrackData:
        return previewTrackData(index, *item);
    }

    return false;
}

bool ProjectBin::stopPreview()
{
    const bool hadPreview = m_previewToken != 0 || m_previewingIndex >= 0 || m_previewTracks;
    if (!hadPreview) {
        return false;
    }

    const int token = m_previewToken;
    m_previewToken = 0;
    m_previewingIndex = -1;

    AudioIO* audioIO = AudioIO::Get();
    if (audioIO && token != 0 && audioIO->IsStreamActive(token)) {
        audioIO->StopStream();
    }

    if (audioIO && audioIO->IsBusy()) {
        if (m_previewPollTimer) {
            m_previewPollTimer->start();
        }
    } else {
        clearPreviewTracks();
    }

    notifyPreviewStateChanged();
    return true;
}

bool ProjectBin::renameItem(int index, const QString& title)
{
    ProjectBinItem* item = itemAt(index);
    if (!item) {
        return false;
    }

    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty() || trimmed == item->title) {
        return false;
    }

    item->title = trimmed;
    m_itemsChanged.notify();
    return true;
}

bool ProjectBin::removeItem(int index)
{
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return false;
    }

    m_items.erase(m_items.begin() + index);
    m_itemsChanged.notify();
    return true;
}

int ProjectBin::referenceCount(int index) const
{
    const ProjectBinItem* item = itemAt(index);
    if (!item) {
        return 0;
    }

    return static_cast<int>(liveInstanceKeys(*item).size());
}

bool ProjectBin::selectAllInstances(int index)
{
    const ProjectBinItem* item = itemAt(index);
    if (!item) {
        return false;
    }

    const trackedit::ClipKeyList instances = liveInstanceKeys(*item);
    if (instances.empty()) {
        return false;
    }

    selectionController()->setSelectedClips(instances);
    return true;
}

bool ProjectBin::isMissing(int index) const
{
    const ProjectBinItem* item = itemAt(index);
    if (!item || item->sourceType != ProjectBinItem::SourceType::FileReference || item->path.empty()) {
        return false;
    }

    return !QFileInfo::exists(item->path.toQString());
}

bool ProjectBin::locateMissingReference(int index)
{
    ProjectBinItem* item = itemAt(index);
    if (!item || item->sourceType != ProjectBinItem::SourceType::FileReference || item->path.empty()) {
        return false;
    }

    const QFileInfo oldFileInfo(item->path.toQString());
    const muse::io::path_t selectedDir = interactive()->selectDirectory("Locate project bin reference", oldFileInfo.absolutePath());
    if (selectedDir.empty()) {
        return false;
    }

    const QString candidate = QDir(selectedDir.toQString()).filePath(oldFileInfo.fileName());
    if (!QFileInfo::exists(candidate)) {
        interactive()->warningSync("Project bin reference not found",
                                   "The selected folder does not contain the missing file.", { muse::IInteractive::Button::Ok },
                                   muse::IInteractive::Button::Ok);
        return false;
    }

    item->path = muse::io::path_t(candidate);
    item->waveform = captureFileWaveform(item->path, item->duration);
    m_itemsChanged.notify();
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

muse::Ret ProjectBin::pasteFileReference(ProjectBinItem& item, const trackedit::TrackIdList& dstTrackIds,
                                         trackedit::secs_t startTime)
{
    if (item.path.empty() || dstTrackIds.empty() || !QFileInfo::exists(item.path.toQString())) {
        return muse::make_ret(muse::Ret::Code::UnknownError);
    }

    const project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return muse::make_ret(muse::Ret::Code::UnknownError);
    }

    const trackedit::ClipKeyList before = allProjectClipKeys();
    const muse::Ret ret = project->importIntoTracks({ item.path }, { dstTrackIds.front() }, startTime);
    if (ret) {
        addInstances(item, before);
    }

    return ret;
}

muse::Ret ProjectBin::pasteTrackData(ProjectBinItem& item, const trackedit::TrackIdList& dstTrackIds,
                                     trackedit::secs_t startTime)
{
    if (item.trackData.empty() || dstTrackIds.empty()) {
        return muse::make_ret(muse::Ret::Code::UnknownError);
    }

    const trackedit::ClipKeyList before = allProjectClipKeys();
    selectionController()->setSelectedTracks(dstTrackIds);

    bool modified = false;
    muse::Ret ret = tracksInteraction()->paste(item.trackData, startTime, false /* moveClips */, false /* moveAllTracks */,
                                               false /* isMultiSelectionCopy */, modified);
    if (!ret) {
        return ret;
    }

    addInstances(item, before);

    if (modified) {
        projectHistory()->pushHistoryState("Dropped project bin item", "Project Bin");
    }

    return ret;
}

std::vector<double> ProjectBin::captureWaveform(const trackedit::ClipKey& clipKey) const
{
    const project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project || !clipKey.isValid()) {
        return {};
    }

    Au3Project* au3Project = reinterpret_cast<Au3Project*>(project->au3ProjectPtr());
    Au3WaveTrack* waveTrack = DomAccessor::findWaveTrack(*au3Project, Au3TrackId(clipKey.trackId));
    if (!waveTrack) {
        return {};
    }

    const std::shared_ptr<Au3WaveClip> waveClip = DomAccessor::findWaveClip(waveTrack, clipKey.itemId);
    if (!waveClip) {
        return {};
    }

    const double start = waveClip->Start();
    const double end = waveClip->End();
    const double duration = end - start;
    if (duration <= 0.0) {
        return {};
    }

    std::vector<double> waveform;
    waveform.reserve(WAVEFORM_BUCKET_COUNT);

    for (int bucket = 0; bucket < WAVEFORM_BUCKET_COUNT; ++bucket) {
        double peak = 0.0;
        for (int sample = 0; sample < WAVEFORM_SAMPLES_PER_BUCKET; ++sample) {
            const double ratio = (static_cast<double>(bucket) + ((static_cast<double>(sample) + 0.5) / WAVEFORM_SAMPLES_PER_BUCKET))
                                 / WAVEFORM_BUCKET_COUNT;
            const double time = start + (duration * ratio);
            const auto sampleOffset = waveClip->TimeToSamples(time);
            const double adjustedTime = waveClip->SamplesToTime(sampleOffset);

            for (size_t channel = 0; channel < waveClip->NChannels(); ++channel) {
                float oneSample = 0.0f;
                if (!WaveClipUtilities::GetFloatAtTime(*waveClip, adjustedTime, channel, oneSample, false)) {
                    continue;
                }

                const double envelope = waveClip->GetEnvelope().GetValue(adjustedTime, 1.0 / waveClip->GetRate());
                peak = std::max(peak, std::abs(static_cast<double>(oneSample) * envelope));
            }
        }

        waveform.push_back(std::clamp(peak, 0.0, 1.0));
    }

    return waveform;
}

std::vector<double> ProjectBin::captureFileWaveform(const muse::io::path_t& path, double duration) const
{
    TrackHolders tracks;
    if (!importFileReferenceTracks(path, tracks)) {
        return {};
    }

    return captureTrackWaveform(tracks, duration);
}

std::vector<double> ProjectBin::captureTrackWaveform(const std::vector<std::shared_ptr<Track> >& tracks, double duration) const
{
    if (tracks.empty() || duration <= 0.0) {
        return {};
    }

    std::vector<double> waveform;
    waveform.reserve(WAVEFORM_BUCKET_COUNT);

    bool sampled = false;
    for (int bucket = 0; bucket < WAVEFORM_BUCKET_COUNT; ++bucket) {
        double peak = 0.0;
        for (int sample = 0; sample < WAVEFORM_SAMPLES_PER_BUCKET; ++sample) {
            const double ratio = (static_cast<double>(bucket) + ((static_cast<double>(sample) + 0.5) / WAVEFORM_SAMPLES_PER_BUCKET))
                                 / WAVEFORM_BUCKET_COUNT;
            const double time = duration * ratio;

            for (const std::shared_ptr<Track>& track : tracks) {
                const auto waveTrack = std::dynamic_pointer_cast<WaveTrack>(track);
                if (!waveTrack) {
                    continue;
                }

                for (const auto& waveClip : waveTrack->Intervals()) {
                    if (waveClip) {
                        sampled = accumulateWaveClipPeak(*waveClip, time, peak) || sampled;
                    }
                }
            }
        }

        waveform.push_back(std::clamp(peak, 0.0, 1.0));
    }

    return sampled ? waveform : std::vector<double> {};
}

bool ProjectBin::importFileReferenceTracks(const muse::io::path_t& path, std::vector<std::shared_ptr<Track> >& tracks) const
{
    tracks.clear();

    if (path.empty() || !QFileInfo::exists(path.toQString())) {
        return false;
    }

    const project::IAudacityProjectPtr currentProject = globalContext()->currentProject();
    if (!currentProject) {
        return false;
    }

    Au3Project* au3Project = reinterpret_cast<Au3Project*>(currentProject->au3ProjectPtr());

    auto oldTags = Tags::Get(*au3Project).shared_from_this();
    auto newTags = oldTags->Duplicate();
    Tags::Set(*au3Project, newTags);

    std::optional<LibFileFormats::AcidizerTags> acidTags;
    TranslatableString errorMessage;
    const wxString wxPath = path.toString().toUtf8().constData();
    const bool ok = Importer::Get().Import(*au3Project, wxPath, nullptr, &WaveTrackFactory::Get(*au3Project), tracks, newTags.get(),
                                           acidTags, errorMessage);
    Tags::Set(*au3Project, oldTags);

    return ok && !tracks.empty();
}

au::trackedit::ClipKeyList ProjectBin::allProjectClipKeys() const
{
    trackedit::ClipKeyList keys;
    const trackedit::ITrackeditProjectPtr project = globalContext()->currentTrackeditProject();
    if (!project) {
        return keys;
    }

    for (trackedit::TrackId trackId : project->trackIdList()) {
        for (const trackedit::Clip& clip : project->clipList(trackId)) {
            if (clip.isValid()) {
                keys.push_back(clip.key);
            }
        }
    }

    return keys;
}

au::trackedit::ClipKeyList ProjectBin::addedClipKeys(const trackedit::ClipKeyList& before) const
{
    trackedit::ClipKeyList added;
    const trackedit::ClipKeyList after = allProjectClipKeys();

    for (const trackedit::ClipKey& key : after) {
        if (std::find(before.begin(), before.end(), key) == before.end()) {
            added.push_back(key);
        }
    }

    return added;
}

au::trackedit::ClipKeyList ProjectBin::liveInstanceKeys(const ProjectBinItem& item) const
{
    trackedit::ClipKeyList live;
    const trackedit::ITrackeditProjectPtr project = globalContext()->currentTrackeditProject();
    if (!project) {
        return live;
    }

    for (const trackedit::ClipKey& key : item.instanceKeys) {
        const trackedit::Clip clip = project->clip(key);
        if (clip.isValid()) {
            live.push_back(key);
        }
    }

    return live;
}

void ProjectBin::addInstances(ProjectBinItem& item, const trackedit::ClipKeyList& before)
{
    const trackedit::ClipKeyList added = addedClipKeys(before);
    if (added.empty()) {
        return;
    }

    item.instanceKeys.insert(item.instanceKeys.end(), added.begin(), added.end());
    m_itemsChanged.notify();
}

bool ProjectBin::previewTrackData(int index, const ProjectBinItem& item)
{
    if (item.trackData.empty() || item.duration <= 0.0) {
        return false;
    }

    const project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return false;
    }

    Au3Project* au3Project = reinterpret_cast<Au3Project*>(project->au3ProjectPtr());
    auto tracks = Au3TrackList::Create(au3Project);

    for (const trackedit::ITrackDataPtr& trackData : item.trackData) {
        const auto au3TrackData = std::dynamic_pointer_cast<trackedit::Au3TrackData>(trackData);
        if (!au3TrackData || !au3TrackData->track()) {
            continue;
        }

        std::shared_ptr<Track> previewTrack = au3TrackData->track()->Duplicate();
        selectTrackForPreview(previewTrack);
        tracks->Add(previewTrack, TrackList::DoAssignId::No, TrackList::EventPublicationSynchrony::Synchronous);
    }

    return playPreviewTracks(index, tracks, item.duration);
}

bool ProjectBin::previewFileReference(int index, const ProjectBinItem& item)
{
    if (item.path.empty() || item.duration <= 0.0) {
        return false;
    }

    if (!QFileInfo::exists(item.path.toQString())) {
        interactive()->warningSync("Missing project bin reference",
                                   "Locate the missing file before previewing it.", { muse::IInteractive::Button::Ok },
                                   muse::IInteractive::Button::Ok);
        return false;
    }

    const project::IAudacityProjectPtr currentProject = globalContext()->currentProject();
    if (!currentProject) {
        return false;
    }

    Au3Project* au3Project = reinterpret_cast<Au3Project*>(currentProject->au3ProjectPtr());

    TrackHolders tmpTracks;
    if (!importFileReferenceTracks(item.path, tmpTracks)) {
        return false;
    }

    auto tracks = Au3TrackList::Create(au3Project);
    for (auto& track : tmpTracks) {
        selectTrackForPreview(track);
        tracks->Add(track, TrackList::DoAssignId::No, TrackList::EventPublicationSynchrony::Synchronous);
    }

    return playPreviewTracks(index, tracks, item.duration);
}

bool ProjectBin::playPreviewTracks(int index, const std::shared_ptr<TrackList>& tracks, double endTime)
{
    if (!tracks || tracks->empty() || endTime <= 0.0) {
        return false;
    }

    const project::IAudacityProjectPtr currentProject = globalContext()->currentProject();
    if (!currentProject) {
        return false;
    }

    AudioIO* audioIO = AudioIO::Get();
    if (!audioIO || audioIO->IsBusy()) {
        return false;
    }

    TransportSequences sequences = makePreviewTransportTracks(*tracks);
    if (sequences.playbackSequences.empty()) {
        return false;
    }

    Au3Project* au3Project = reinterpret_cast<Au3Project*>(currentProject->au3ProjectPtr());
    AudioIOStartStreamOptions options = ProjectAudioIO::GetDefaultOptions(*au3Project);
    const int token = audioIO->StartStream(sequences, 0.0, endTime, endTime, options);
    if (token == 0) {
        LOGE() << "Project bin preview failed to start";
        return false;
    }

    m_previewTracks = tracks;
    m_previewToken = token;
    m_previewingIndex = index;
    if (m_previewPollTimer) {
        m_previewPollTimer->start();
    }
    notifyPreviewStateChanged();

    return true;
}

void ProjectBin::pollPreview()
{
    if (m_previewToken == 0 && !m_previewTracks) {
        if (m_previewPollTimer) {
            m_previewPollTimer->stop();
        }
        return;
    }

    AudioIO* audioIO = AudioIO::Get();
    if (audioIO) {
        if (m_previewToken != 0 && audioIO->IsStreamActive(m_previewToken)) {
            return;
        }

        if (audioIO->IsBusy()) {
            return;
        }
    }

    clearPreviewTracks();
}

void ProjectBin::clearPreviewTracks()
{
    const bool wasPreviewing = m_previewToken != 0 || m_previewingIndex >= 0;

    if (m_previewPollTimer) {
        m_previewPollTimer->stop();
    }

    m_previewTracks.reset();
    m_previewToken = 0;
    m_previewingIndex = -1;

    if (wasPreviewing) {
        notifyPreviewStateChanged();
    }
}

void ProjectBin::notifyPreviewStateChanged()
{
    m_previewStateChanged.notify();
}
