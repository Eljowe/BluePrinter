#include "TakeRecorder.h"

#include "CaptureWrite.h"
#include "LoopPlayback.h"

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
    if (! reviewPlaying.load (std::memory_order_acquire))
        return false;

    const auto audio = selectedAudio;
    const auto length = selectedLength.load (std::memory_order_acquire);
    auto readPos = static_cast<int> (reviewPos.load (std::memory_order_acquire));

    if (audio == nullptr || length <= 0 || readPos >= length)
    {
        stopReview();
        return false;
    }

    const int channels = juce::jmin (dest.getNumChannels(), audio->getNumChannels());
    const int toCopy   = juce::jmin (numSamples, static_cast<int> (length - readPos));

    for (int ch = 0; ch < channels; ++ch)
        dest.copyFrom (ch, 0, *audio, ch, readPos, toCopy);

    // Fill the rest of the block with silence if playback ends mid-block.
    if (toCopy < numSamples)
        for (int ch = 0; ch < dest.getNumChannels(); ++ch)
            dest.clear (ch, toCopy, numSamples - toCopy);

    readPos += toCopy;
    reviewPos.store (readPos, std::memory_order_release);

    if (readPos >= length)
        stopReview();

    return true;
}

void TakeRecorder::renderOverdubMonitor (juce::AudioBuffer<float>& dest, int numSamples,
                                         int declickSamples)
{
    if (! overdubCapture.load (std::memory_order_acquire))
        return;

    const auto audio = selectedAudio;
    const auto takeLen = selectedLength.load (std::memory_order_acquire);
    if (audio == nullptr || takeLen <= 0)
        return;

    const int declick = juce::jmin (declickSamples, static_cast<int> (takeLen / 2));
    const auto position = LoopPlayback::render (
        dest, *audio, 0, takeLen,
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
    if (selectedIndex < 0 || selectedIndex >= static_cast<int> (takes.size()))
    {
        selectedId.store (-1, std::memory_order_release);
        selectedLength.store (0, std::memory_order_release);
        selectedAudio.reset();
        return;
    }

    const auto& t = takes[static_cast<size_t> (selectedIndex)];
    selectedId.store (t.id, std::memory_order_release);
    selectedLength.store (t.length, std::memory_order_release);
    selectedAudio = t.audio;
}

void TakeRecorder::addTake (std::shared_ptr<juce::AudioBuffer<float>> audio,
                            int64_t length, std::vector<float> peaks)
{
    if (isMutatingBlocked())
        return;

    Take take;
    take.id = nextTakeId++;
    take.length = length;
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
    t.audio = std::move (audio);
    t.length = length;
    t.peaks = std::move (peaks);
    refreshSelectedAtomics();
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
}

void TakeRecorder::clearTakes()
{
    if (isMutatingBlocked())
        return;

    stopReview();
    takes.clear();
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

void TakeRecorder::startReview()
{
    reviewPos.store (0, std::memory_order_release);
    reviewPlaying.store (true, std::memory_order_release);
}

void TakeRecorder::stopReview()
{
    reviewPlaying.store (false, std::memory_order_release);
    reviewPos.store (0, std::memory_order_release);
}
