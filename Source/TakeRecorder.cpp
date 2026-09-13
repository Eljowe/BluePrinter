#include "TakeRecorder.h"

#include "CaptureWrite.h"
#include "LoopPlayback.h"

bool TakeRecorder::wantsOverdub() const
{
    return overdub.load (std::memory_order_acquire)
        && takePending.load (std::memory_order_acquire)
        && takeLength.load (std::memory_order_acquire) > 0;
}

void TakeRecorder::armCountIn()
{
    preRollActive.store (true, std::memory_order_release);
    state.store (State::Recording, std::memory_order_release);
    finalizePending.store (false, std::memory_order_release);
}

void TakeRecorder::beginFresh (juce::AudioBuffer<float>& buffer)
{
    // A new take wipes the capture buffer, so any pending (unsaved) take is
    // invalidated. Audio-thread safe: atomics only — takePeaks stays stale
    // until the next finalize rebuilds it.
    takePending.store (false, std::memory_order_release);
    overdubCapture.store (false, std::memory_order_release);
    takePlaybackActive.store (false, std::memory_order_release);
    takePlaybackPos.store (0, std::memory_order_release);
    takeLength.store (0, std::memory_order_release);

    buffer.clear();
    writePos.store (0, std::memory_order_release);
    state.store (State::Recording, std::memory_order_release);
    finalizePending.store (false, std::memory_order_release);
    recordingRequested.store (true, std::memory_order_release);
}

void TakeRecorder::beginOverdub()
{
    // Layering keeps the pending take (and its audio) intact. The new input is
    // captured after the take (writePos = takeLength) and wrap-mixed into
    // [0, takeLength) on stop. The take playback starts from the top so the
    // layer aligns with the take's first sample.
    takePlaybackActive.store (false, std::memory_order_release);
    takePlaybackPos.store (0, std::memory_order_release);
    overdubPlayPos.store (0, std::memory_order_release);
    overdubCapture.store (true, std::memory_order_release);

    writePos.store (takeLength.load (std::memory_order_acquire),
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

bool TakeRecorder::renderReview (juce::AudioBuffer<float>& dest,
                                 const juce::AudioBuffer<float>& buffer, int numSamples)
{
    const auto length = takeLength.load (std::memory_order_acquire);
    if (length <= 0)
    {
        takePlaybackActive.store (false, std::memory_order_release);
        takePlaybackPos.store (0, std::memory_order_release);
        return false;
    }

    auto readPos = static_cast<int> (takePlaybackPos.load (std::memory_order_acquire));
    if (readPos >= length)
    {
        takePlaybackActive.store (false, std::memory_order_release);
        takePlaybackPos.store (0, std::memory_order_release);
        return false;
    }

    const int channels = juce::jmin (dest.getNumChannels(), buffer.getNumChannels());
    const int toCopy   = juce::jmin (numSamples, static_cast<int> (length - readPos));

    for (int ch = 0; ch < channels; ++ch)
        dest.copyFrom (ch, 0, buffer, ch, readPos, toCopy);

    // Fill the rest of the block with silence if playback ends mid-block.
    if (toCopy < numSamples)
    {
        for (int ch = 0; ch < dest.getNumChannels(); ++ch)
            dest.clear (ch, toCopy, numSamples - toCopy);
    }

    readPos += toCopy;
    takePlaybackPos.store (readPos, std::memory_order_release);

    if (readPos >= length)
    {
        takePlaybackActive.store (false, std::memory_order_release);
        takePlaybackPos.store (0, std::memory_order_release);
    }

    return true;
}

void TakeRecorder::renderOverdubMonitor (juce::AudioBuffer<float>& dest,
                                         const juce::AudioBuffer<float>& buffer,
                                         int numSamples, int declickSamples)
{
    const auto takeLen = takeLength.load (std::memory_order_acquire);
    if (takeLen <= 0)
        return;

    const int declick = juce::jmin (declickSamples, static_cast<int> (takeLen / 2));
    const auto position = LoopPlayback::render (
        dest, buffer, 0, takeLen,
        overdubPlayPos.load (std::memory_order_acquire),
        true, 1.0f, declick);

    overdubPlayPos.store (position, std::memory_order_release);
}

void TakeRecorder::finish()
{
    recordingRequested.store (false, std::memory_order_release);
    state.store (State::Idle, std::memory_order_release);
    finalizePending.store (true, std::memory_order_release);
}

void TakeRecorder::clearPending()
{
    takePending.store (false, std::memory_order_release);
    takePlaybackActive.store (false, std::memory_order_release);
    takePlaybackPos.store (0, std::memory_order_release);
    takeLength.store (0, std::memory_order_release);
    overdubPending.store (false, std::memory_order_release);
    overdubCapture.store (false, std::memory_order_release);
    overdubPlayPos.store (0, std::memory_order_release);
    takePeaks.clear();
}

void TakeRecorder::setPendingTake (int64_t captured)
{
    takePlaybackActive.store (false, std::memory_order_release);
    takePlaybackPos.store (0, std::memory_order_release);
    takeLength.store (captured, std::memory_order_release);
    takePending.store (true, std::memory_order_release);
}

bool TakeRecorder::canStartReview() const
{
    if (! takePending.load (std::memory_order_acquire)
        || takeLength.load (std::memory_order_acquire) <= 0)
        return false;

    // Not while a capture (fresh or layered) is in flight — review playback
    // would fight the capture/overdub playback.
    return ! isRecordingRequested()
        && ! overdubCapture.load (std::memory_order_acquire);
}

void TakeRecorder::startReview()
{
    takePlaybackPos.store (0, std::memory_order_release);
    takePlaybackActive.store (true, std::memory_order_release);
}

void TakeRecorder::stopReview()
{
    takePlaybackActive.store (false, std::memory_order_release);
    takePlaybackPos.store (0, std::memory_order_release);
}
