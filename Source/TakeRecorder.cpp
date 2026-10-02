#include "TakeRecorder.h"

#include "CaptureWrite.h"
#include "CaptureCopy.h"
#include "LoopPlayback.h"
#include <algorithm>

namespace
{
const std::vector<float>& emptyPeaks()
{
    static const std::vector<float> empty;
    return empty;
}
}

//==============================================================================
bool TakeRecorder::wantsOverdub() const
{
    return overdub.load (std::memory_order_acquire)
        && hasSelectedTake()
        && selectedLength.load (std::memory_order_acquire) > 0;
}

std::vector<TakeRecorder::TakeView> TakeRecorder::getTakeList() const
{
    std::vector<TakeView> list;
    list.reserve (takes.size());
    for (const auto& t : takes)
        list.push_back ({ t.id, t.length });
    return list;
}

const std::vector<float>& TakeRecorder::getSelectedTakePeaks() const
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int> (takes.size()))
        return emptyPeaks();
    return takes[static_cast<size_t> (selectedIndex)].peaks;
}

std::shared_ptr<juce::AudioBuffer<float>> TakeRecorder::getSelectedTakeAudio() const
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int> (takes.size()))
        return nullptr;
    return takes[static_cast<size_t> (selectedIndex)].audio;
}

std::shared_ptr<juce::AudioBuffer<float>> TakeRecorder::getSelectedTrimmedAudio() const
{
    auto audio = getSelectedTakeAudio();
    if (audio == nullptr)
        return nullptr;
    if (getTrimStart() == 0 && getTrimEnd() == audio->getNumSamples())
        return audio;
    return CaptureCopy::copyRegion (*audio, getTrimStart(), getTrimEnd() - getTrimStart());
}

//==============================================================================
void TakeRecorder::armCountIn()
{
    preRollActive.store (true, std::memory_order_release);
    state.store (State::Recording, std::memory_order_release);
    finalizePending.store (false, std::memory_order_release);
}

void TakeRecorder::beginFresh (juce::AudioBuffer<float>& buffer)
{
    // A new take does NOT invalidate the stack (0037): its audio lives in the
    // record buffer only until finalize, when it is copied out.
    stopReview();
    overdubCapture.store (false, std::memory_order_release);
    overdubPlayPos.store (0, std::memory_order_release);

    buffer.clear();
    writePos.store (0, std::memory_order_release);
    state.store (State::Recording, std::memory_order_release);
    finalizePending.store (false, std::memory_order_release);
    recordingRequested.store (true, std::memory_order_release);
}

void TakeRecorder::beginOverdub (juce::AudioBuffer<float>& buffer)
{
    // Layer onto the selected take: stage its audio into the record buffer,
    // capture the new input after it (writePos = selected length), and let the
    // processor wrap-mix the layer back on stop.
    if (! hasSelectedTake())
        return;

    stopReview();
    overdubPlayPos.store (0, std::memory_order_release);
    overdubCapture.store (true, std::memory_order_release);

    buffer.clear();
    if (const auto src = selectedAudio)
    {
        const auto length = selectedLength.load (std::memory_order_acquire);
        const int channels = juce::jmin (buffer.getNumChannels(), src->getNumChannels());
        const int count = static_cast<int> (juce::jmin<int64_t> (length, buffer.getNumSamples()));
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom (ch, 0, *src, ch, 0, count);
    }

    writePos.store (selectedLength.load (std::memory_order_acquire),
                    std::memory_order_release);
    state.store (State::Recording, std::memory_order_release);
    finalizePending.store (false, std::memory_order_release);
    recordingRequested.store (true, std::memory_order_release);
}

void TakeRecorder::cancelCountIn()
{
    preRollActive.store (false, std::memory_order_release);
    overdubPending.store (false, std::memory_order_release);
    state.store (State::Idle, std::memory_order_release);
}

void TakeRecorder::write (juce::AudioBuffer<float>& buffer, int maxSamples,
                          const juce::AudioBuffer<float>& source, int numSamples)
{
    const auto pos = CaptureWrite::write (buffer,
                                          writePos.load (std::memory_order_acquire),
                                          source, numSamples, maxSamples);
    writePos.store (pos, std::memory_order_release);

    if (pos >= maxSamples)
    {
        recordingRequested.store (false, std::memory_order_release);
        state.store (State::Idle, std::memory_order_release);
        finalizePending.store (true, std::memory_order_release);
    }
}

bool TakeRecorder::renderReview (juce::AudioBuffer<float>& dest, int numSamples)
{
    const AudioRead reader (*this);
    if (! reader.allowed)
        return false;
    if (! reviewPlaying.load (std::memory_order_acquire))
        return false;

    const auto audio = selectedAudio;
    const auto start = getTrimStart();
    const auto end = getTrimEnd();
    if (audio == nullptr || end <= start)
    {
        stopReview();
        return false;
    }

    const auto seek = pendingReviewSeek.exchange (-1, std::memory_order_acq_rel);
    auto readPos = static_cast<int> (juce::jlimit (start, end - 1,
        seek >= 0 ? seek : reviewPos.load (std::memory_order_acquire)));

    const int channels = juce::jmin (dest.getNumChannels(), audio->getNumChannels());
    const int toCopy   = juce::jmin (numSamples, static_cast<int> (end - readPos));

    // Review replaces the monitor mix, including unmapped channels and the
    // remainder of a block that reaches the trim end.
    dest.clear (0, numSamples);

    for (int ch = 0; ch < channels; ++ch)
        dest.copyFrom (ch, 0, *audio, ch, readPos, toCopy);

    readPos += toCopy;
    reviewPos.store (readPos, std::memory_order_release);

    if (readPos >= end)
        reviewPlaying.store (false, std::memory_order_release);

    return true;
}

void TakeRecorder::renderOverdubMonitor (juce::AudioBuffer<float>& dest, int numSamples,
                                         int declickSamples)
{
    const AudioRead reader (*this);
    if (! reader.allowed)
        return;
    if (! overdubCapture.load (std::memory_order_acquire))
        return;

    const auto audio = selectedAudio;
    const auto takeStart = getTrimStart();
    const auto takeLen = getTrimEnd() - takeStart;
    if (audio == nullptr || takeLen <= 0)
        return;

    const int declick = juce::jmin (declickSamples, static_cast<int> (takeLen / 2));
    const auto position = LoopPlayback::render (
        dest, *audio, takeStart, takeLen,
        overdubPlayPos.load (std::memory_order_acquire),
        true, 1.0f, declick);

    overdubPlayPos.store (position, std::memory_order_release);
}

//==============================================================================
void TakeRecorder::finish()
{
    recordingRequested.store (false, std::memory_order_release);
    state.store (State::Idle, std::memory_order_release);
    finalizePending.store (true, std::memory_order_release);
}

//==============================================================================
size_t TakeRecorder::totalBytes() const
{
    size_t bytes = 0;
    for (const auto& t : takes)
        if (t.audio != nullptr)
            bytes += static_cast<size_t> (t.audio->getNumSamples())
                   * static_cast<size_t> (t.audio->getNumChannels())
                   * sizeof (float);
    return bytes;
}

void TakeRecorder::refreshSelectedAtomics()
{
    audioChanging.store (true);
    while (audioReaders.load() != 0)
        juce::Thread::yield();

    if (selectedIndex < 0 || selectedIndex >= static_cast<int> (takes.size()))
    {
        selectedId.store (-1, std::memory_order_release);
        selectedLength.store (0, std::memory_order_release);
        selectedAudio.reset();
        selectedTrimStart.store (0, std::memory_order_release);
        selectedTrimEnd.store (0, std::memory_order_release);
        audioChanging.store (false);
        return;
    }

    const auto& t = takes[static_cast<size_t> (selectedIndex)];
    selectedId.store (t.id, std::memory_order_release);
    selectedLength.store (t.length, std::memory_order_release);
    selectedAudio = t.audio;
    selectedTrimStart.store (t.trimStart, std::memory_order_release);
    selectedTrimEnd.store (t.trimEnd, std::memory_order_release);
    reviewPos.store (t.trimStart, std::memory_order_release);
    pendingReviewSeek.store (-1, std::memory_order_release);
    audioChanging.store (false);
}

void TakeRecorder::addTake (std::shared_ptr<juce::AudioBuffer<float>> audio,
                            int64_t length, std::vector<float> peaks)
{
    if (isMutatingBlocked())
        return;

    if (audio == nullptr || length <= 0 || length > audio->getNumSamples())
        return;
    stopReview();

    Take take;
    take.id = nextTakeId++;
    take.length = length;
    take.trimEnd = length;
    take.peaks = std::move (peaks);
    take.audio = std::move (audio);
    takes.push_back (std::move (take));

    // Enforce the bounds, never dropping the take just added.
    while (static_cast<int> (takes.size()) > maxTakes
           || (totalBytes() > maxTakeBytes && takes.size() > 1))
    {
        takes.erase (takes.begin());
        ++droppedCount;
    }

    // Auto-select the new take.
    selectedIndex = static_cast<int> (takes.size()) - 1;
    refreshSelectedAtomics();
    pruneUndoHistory();
}

void TakeRecorder::replaceSelectedTake (std::shared_ptr<juce::AudioBuffer<float>> audio,
                                        int64_t length, std::vector<float> peaks)
{
    // Overdub finalize consumes overdubCapture before calling this, so the
    // guard passes; it keeps the "no mutation mid-capture" contract explicit.
    if (isMutatingBlocked())
        return;
    if (selectedIndex < 0 || selectedIndex >= static_cast<int> (takes.size()))
        return;

    auto& t = takes[static_cast<size_t> (selectedIndex)];
    if (audio == nullptr || length <= 0 || length > audio->getNumSamples())
        return;
    stopReview();
    const auto snapshotBytes = static_cast<size_t> (t.audio->getNumSamples())
                             * static_cast<size_t> (t.audio->getNumChannels()) * sizeof (float);
    if (snapshotBytes <= maxUndoBytes)
        undoHistory.push_back ({ t.id, t.length, std::move (t.peaks), std::move (t.audio) });
    t.audio = std::move (audio);
    t.length = length;
    t.peaks = std::move (peaks);
    t.trimStart = juce::jlimit<int64_t> (0, length - 1, t.trimStart);
    t.trimEnd = juce::jlimit (t.trimStart + 1, length, t.trimEnd);
    refreshSelectedAtomics();
    pruneUndoHistory();
}

void TakeRecorder::selectTake (int id)
{
    if (isMutatingBlocked())
        return;

    for (size_t i = 0; i < takes.size(); ++i)
    {
        if (takes[i].id != id)
            continue;
        if (selectedIndex == static_cast<int> (i))
            return;

        stopReview();
        selectedIndex = static_cast<int> (i);
        refreshSelectedAtomics();
        return;
    }
}

void TakeRecorder::pruneUndoHistory()
{
    undoHistory.erase (std::remove_if (undoHistory.begin(), undoHistory.end(), [this](const UndoSnapshot& s)
    {
        return std::none_of (takes.begin(), takes.end(), [&s](const Take& t) { return t.id == s.takeId; });
    }), undoHistory.end());

    size_t bytes = 0;
    for (const auto& snapshot : undoHistory)
        bytes += static_cast<size_t> (snapshot.audio->getNumSamples())
               * static_cast<size_t> (snapshot.audio->getNumChannels()) * sizeof (float);
    while (! undoHistory.empty() && (undoHistory.size() > maxUndoLayers || bytes > maxUndoBytes))
    {
        const auto& oldest = undoHistory.front();
        bytes -= static_cast<size_t> (oldest.audio->getNumSamples())
               * static_cast<size_t> (oldest.audio->getNumChannels()) * sizeof (float);
        undoHistory.erase (undoHistory.begin());
    }
}

bool TakeRecorder::canUndoOverdub() const
{
    return std::any_of (undoHistory.begin(), undoHistory.end(), [this](const UndoSnapshot& s)
    {
        return s.takeId == getSelectedTakeId();
    });
}

bool TakeRecorder::undoOverdub()
{
    if (isMutatingBlocked() || isReviewPlaying() || ! hasSelectedTake())
        return false;
    for (size_t i = undoHistory.size(); i > 0; --i)
    {
        auto& snapshot = undoHistory[i - 1];
        if (snapshot.takeId != getSelectedTakeId())
            continue;
        auto& t = takes[static_cast<size_t> (selectedIndex)];
        stopReview();
        t.audio = std::move (snapshot.audio);
        t.peaks = std::move (snapshot.peaks);
        t.length = snapshot.length;
        t.trimStart = juce::jlimit<int64_t> (0, t.length - 1, t.trimStart);
        t.trimEnd = juce::jlimit (t.trimStart + 1, t.length, t.trimEnd);
        undoHistory.erase (undoHistory.begin() + static_cast<std::ptrdiff_t> (i - 1));
        refreshSelectedAtomics();
        return true;
    }
    return false;
}

bool TakeRecorder::setTrim (int64_t start, int64_t end)
{
    if (isMutatingBlocked() || ! hasSelectedTake())
        return false;
    auto& t = takes[static_cast<size_t> (selectedIndex)];
    start = juce::jlimit<int64_t> (0, t.length - 1, start);
    end = juce::jlimit (start + 1, t.length, end);
    if (t.trimStart == start && t.trimEnd == end)
        return false;
    stopReview();
    t.trimStart = start;
    t.trimEnd = end;
    refreshSelectedAtomics();
    return true;
}

void TakeRecorder::deleteTake (int id)
{
    if (isMutatingBlocked())
        return;

    int index = -1;
    for (size_t i = 0; i < takes.size(); ++i)
        if (takes[i].id == id)
        {
            index = static_cast<int> (i);
            break;
        }
    if (index < 0)
        return;

    stopReview();
    takes.erase (takes.begin() + index);

    if (takes.empty())
    {
        selectedIndex = -1;
    }
    else if (selectedIndex == index)
    {
        selectedIndex = juce::jmin (index, static_cast<int> (takes.size()) - 1);
    }
    else if (selectedIndex > index)
    {
        --selectedIndex;
    }

    refreshSelectedAtomics();
    pruneUndoHistory();
}

void TakeRecorder::clearTakes()
{
    if (isMutatingBlocked())
        return;

    stopReview();
    takes.clear();
    undoHistory.clear();
    selectedIndex = -1;
    droppedCount = 0;
    overdubPending.store (false, std::memory_order_release);
    overdubPlayPos.store (0, std::memory_order_release);
    refreshSelectedAtomics();
}

int TakeRecorder::consumeDroppedCount()
{
    const int n = droppedCount;
    droppedCount = 0;
    return n;
}

//==============================================================================
bool TakeRecorder::canStartReview() const
{
    if (! hasSelectedTake()
        || selectedLength.load (std::memory_order_acquire) <= 0)
        return false;

    // Not while a capture (fresh or layered) is in flight — review playback
    // would fight the capture/overdub playback.
    return ! isActive()
        && ! overdubCapture.load (std::memory_order_acquire);
}

void TakeRecorder::startReview (int64_t startSample)
{
    if (! canStartReview())
        return;
    stopReview();
    refreshSelectedAtomics();
    reviewPos.store (juce::jlimit (getTrimStart(), getTrimEnd() - 1,
                                 startSample < 0 ? getTrimStart() : startSample), std::memory_order_release);
    pendingReviewSeek.store (-1, std::memory_order_release);
    reviewPlaying.store (true, std::memory_order_release);
}

void TakeRecorder::stopReview()
{
    reviewPlaying.store (false, std::memory_order_release);
    pendingReviewSeek.store (-1, std::memory_order_release);
    reviewPos.store (getTrimStart(), std::memory_order_release);
}

void TakeRecorder::seekReview (int64_t position)
{
    if (canStartReview() && isReviewPlaying())
        pendingReviewSeek.store (juce::jlimit (getTrimStart(), getTrimEnd() - 1, position),
                                 std::memory_order_release);
}
