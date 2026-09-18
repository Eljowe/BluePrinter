#include "Looper.h"

#include "CaptureCopy.h"
#include "CaptureWrite.h"
#include "LoopPlayback.h"

namespace
{
size_t snapshotBytes (const juce::AudioBuffer<float>& b)
{
    return static_cast<size_t> (b.getNumSamples())
         * static_cast<size_t> (b.getNumChannels())
         * sizeof (float);
}
}

bool Looper::isIdle() const
{
    return ! (isPlaying()
           || isRecording()
           || isPreRollActive()
           || isOverdubCapture()
           || isCaptureArmed());
}

void Looper::captureBlock (juce::AudioBuffer<float>& buffer, int maxSamples,
                           const juce::AudioBuffer<float>& source, int numSamples,
                           int64_t targetSamples)
{
    if (! captureArmed.load (std::memory_order_acquire))
        return;

    const bool overdubCaptureNow = overdubCapture.load (std::memory_order_acquire);
    const auto writePos = overdubCaptureNow
        ? overdubWritePos.load (std::memory_order_relaxed)
        : length.load (std::memory_order_relaxed);

    const auto newWritePos = CaptureWrite::write (buffer, writePos, source,
                                                  numSamples, maxSamples);
    if (newWritePos != writePos)
    {
        if (overdubCaptureNow)
            overdubWritePos.store (newWritePos, std::memory_order_release);
        else
            length.store (newWritePos, std::memory_order_release);
    }

    // Fixed-length capture (0022): once a fresh capture has reached the
    // configured number of bars, stop writing and ask the message thread to
    // finalise it. Overdub layers are never auto-stopped — the existing loop
    // already defines the length.
    if (! overdubCaptureNow)
    {
        if (targetSamples > 0 && length.load (std::memory_order_acquire) >= targetSamples)
        {
            captureArmed.store (false, std::memory_order_release);
            recording.store (false, std::memory_order_release);
            autoStopPending.store (true, std::memory_order_release);
        }
    }
}

bool Looper::renderPlayback (juce::AudioBuffer<float>& dest,
                             const juce::AudioBuffer<float>& buffer,
                             int numSamples, float gain, int declickSamples,
                             float& outBlockPeak)
{
    outBlockPeak = 0.0f;

    if (! playing.load (std::memory_order_acquire))
        return false;

    const auto loopStart = start.load (std::memory_order_acquire);
    const auto loopLength = length.load (std::memory_order_acquire);
    if (loopLength <= 0)
        return false;

    const int declick = juce::jmin (declickSamples, static_cast<int> (loopLength / 2));
    const bool isLooping = looping.load (std::memory_order_acquire);

    // Reverse / half-speed are playback-only transforms (0036). Ignored while
    // an overdub is capturing: the layer is aligned to the forward downbeat.
    const bool overdubCaptureNow = overdubCapture.load (std::memory_order_acquire);
    const LoopPlayback::PlaybackMode mode {
        ! overdubCaptureNow && playbackReverse.load (std::memory_order_acquire),
        (! overdubCaptureNow && playbackHalfSpeed.load (std::memory_order_acquire)) ? 0.5 : 1.0
    };

    float blockPeak = 0.0f;
    const auto newPosition = LoopPlayback::renderMode (
        dest, buffer, loopStart, loopLength,
        position.load (std::memory_order_acquire),
        isLooping, gain, declick, mode, &blockPeak);

    outBlockPeak = blockPeak;
    position.store (newPosition, std::memory_order_release);

    if (! isLooping && newPosition >= static_cast<double> (loopLength))
        playing.store (false, std::memory_order_release);

    return true;
}

void Looper::beginCapture()
{
    preRollActive.store (false, std::memory_order_release);
    captureArmed.store (true, std::memory_order_release);
    recording.store (true, std::memory_order_release);

    if (! overdubCapture.load (std::memory_order_acquire))
    {
        length.store (0, std::memory_order_release);
    }
    else
    {
        position.store (0, std::memory_order_release);
        playing.store (true, std::memory_order_release);
    }
}

void Looper::armFresh()
{
    // Fresh capture: wipe the loop and start from sample 0. The layer
    // history no longer applies (0030).
    resetLoopState();
    peaks.clear();
}

void Looper::resetLoopState()
{
    clearHistory();
    autoStopPending.store (false, std::memory_order_release);
    preRollActive.store (false, std::memory_order_release);
    captureArmed.store (false, std::memory_order_release);
    recording.store (false, std::memory_order_release);
    overdubCapture.store (false, std::memory_order_release);
    overdubWritePos.store (0, std::memory_order_release);
    playing.store (false, std::memory_order_release);
    start.store (0, std::memory_order_release);
    length.store (0, std::memory_order_release);
    fullLength.store (0, std::memory_order_release);
    position.store (0, std::memory_order_release);
    cropStartBeats = 0;
    cropEndBeats = 0;
}

void Looper::armOverdub (bool countIn)
{
    // Layer over the existing loop: keep the loop intact and write the new
    // input into the region after it (the audio thread taps overdubWritePos
    // while length stays fixed, so the wrap boundary never moves). The layer
    // is mixed into the loop on stop. Base it after the FULL loop, not after
    // the cropped window: with a start crop the window ends at
    // start + length > length, so a layer based at length would overwrite the
    // loop's tail.
    preRollActive.store (false, std::memory_order_release);
    captureArmed.store (false, std::memory_order_release);
    recording.store (false, std::memory_order_release);
    overdubCapture.store (true, std::memory_order_release);
    overdubWritePos.store (fullLength.load (std::memory_order_acquire),
                           std::memory_order_release);
    position.store (0, std::memory_order_release);
    // Overdub aligns to the forward downbeat: drop any reverse / half-speed.
    playbackReverse.store (false, std::memory_order_release);
    playbackHalfSpeed.store (false, std::memory_order_release);
    // With a count-in, hold the loop silent through it and start it from
    // position 0 when the capture arms (beginCapture).
    playing.store (! countIn, std::memory_order_release);
}

void Looper::armCaptureNow()
{
    captureArmed.store (true, std::memory_order_release);
    recording.store (true, std::memory_order_release);
}

void Looper::stopCapture()
{
    autoStopPending.store (false, std::memory_order_release);
    preRollActive.store (false, std::memory_order_release);
    captureArmed.store (false, std::memory_order_release);
    recording.store (false, std::memory_order_release);
}

void Looper::setGridTrimmed (int64_t target)
{
    start.store (0, std::memory_order_release);
    length.store (target, std::memory_order_release);
    // The grid-trimmed capture is the reference every future crop is measured
    // against (setCrop), so crop changes stay reversible.
    fullLength.store (target, std::memory_order_release);
    cropStartBeats = 0;
    cropEndBeats = 0;
}

void Looper::setLoaded (int64_t loadedLength)
{
    const auto clamped = juce::jmax<int64_t> (0, loadedLength);
    resetLoopState();
    length.store (clamped, std::memory_order_release);
    fullLength.store (clamped, std::memory_order_release);
}

bool Looper::setCrop (int startBeats, int endBeats, double sampleRate, float bpm,
                      LooperGrid::TimeSignature meter)
{
    const auto full = fullLength.load (std::memory_order_acquire);
    if (full <= 0 || sampleRate <= 0.0)
        return false;

    // Crop math (whole beats measured against the FULL loop, so moving a
    // handle back restores what it cut) lives in LooperGrid::computeCrop.
    const auto crop = LooperGrid::computeCrop (full, sampleRate, bpm,
                                               startBeats, endBeats, meter);
    cropStartBeats = crop.startBeats;
    cropEndBeats   = crop.endBeats;

    start.store  (crop.startSamples,  std::memory_order_release);
    length.store (crop.lengthSamples, std::memory_order_release);

    // Keep the playhead inside the cropped window.
    const auto remaining = length.load (std::memory_order_acquire);
    position.store (juce::jmin (position.load (std::memory_order_acquire),
                                static_cast<double> (juce::jmax<int64_t> (0, remaining - 1))),
                    std::memory_order_release);
    if (remaining <= 0)
        playing.store (false, std::memory_order_release);

    return true;
}

void Looper::setPlaying (bool enabled)
{
    position.store (0, std::memory_order_release);
    playing.store (enabled && length.load (std::memory_order_acquire) > 0,
                   std::memory_order_release);
}

void Looper::setOverdub (bool enabled)
{
    overdub.store (enabled, std::memory_order_release);
    if (enabled)
    {
        // Overdub aligns to the forward downbeat, so drop reverse / half-speed
        // playback when it is switched on (0036).
        playbackReverse.store (false, std::memory_order_release);
        playbackHalfSpeed.store (false, std::memory_order_release);
    }
}

void Looper::clear()
{
    clearHistory();
    autoStopPending.store (false, std::memory_order_release);
    preRollActive.store (false, std::memory_order_release);
    captureArmed.store (false, std::memory_order_release);
    recording.store (false, std::memory_order_release);
    overdubCapture.store (false, std::memory_order_release);
    overdubWritePos.store (0, std::memory_order_release);
    playing.store (false, std::memory_order_release);
    start.store (0, std::memory_order_release);
    length.store (0, std::memory_order_release);
    fullLength.store (0, std::memory_order_release);
    position.store (0, std::memory_order_release);
    cropStartBeats = 0;
    cropEndBeats = 0;
    peaks.clear();
}

void Looper::clearHistory()
{
    undoStack.clear();
    redoStack.clear();
    undoBytes = 0;
}

void Looper::pushUndoSnapshot (const juce::AudioBuffer<float>& buffer)
{
    const auto full = fullLength.load (std::memory_order_acquire);
    if (full <= 0)
        return;

    auto snapshot = CaptureCopy::copyRegion (buffer, 0, full);
    if (snapshot == nullptr)
        return;

    undoStack.push_back (snapshot);
    undoBytes += snapshotBytes (*snapshot);
    trimHistoryStacks();
    redoStack.clear();
}

bool Looper::undo (juce::AudioBuffer<float>& buffer)
{
    if (undoStack.empty())
        return false;

    const auto full = fullLength.load (std::memory_order_acquire);
    if (full <= 0)
        return false;

    if (auto current = CaptureCopy::copyRegion (buffer, 0, full))
        redoStack.push_back (current);

    const auto snapshot = undoStack.back();
    undoStack.pop_back();
    undoBytes -= snapshotBytes (*snapshot);

    trimHistoryStacks();
    applySnapshot (buffer, snapshot);
    return true;
}

bool Looper::redo (juce::AudioBuffer<float>& buffer)
{
    if (redoStack.empty())
        return false;

    const auto full = fullLength.load (std::memory_order_acquire);
    if (full <= 0)
        return false;

    if (auto current = CaptureCopy::copyRegion (buffer, 0, full))
    {
        undoStack.push_back (current);
        undoBytes += snapshotBytes (*current);
    }

    const auto snapshot = redoStack.back();
    redoStack.pop_back();

    trimHistoryStacks();
    applySnapshot (buffer, snapshot);
    return true;
}

void Looper::applySnapshot (juce::AudioBuffer<float>& buffer,
                            const std::shared_ptr<juce::AudioBuffer<float>>& snapshot)
{
    const auto full = fullLength.load (std::memory_order_acquire);
    if (snapshot == nullptr || full <= 0)
        return;

    const int channels = juce::jmin (buffer.getNumChannels(), snapshot->getNumChannels());
    const int count = static_cast<int> (juce::jmin (full, static_cast<int64_t> (snapshot->getNumSamples())));
    for (int ch = 0; ch < channels; ++ch)
        buffer.copyFrom (ch, 0, *snapshot, ch, 0, count);
}

void Looper::trimHistoryStacks()
{
    while (undoStack.size() > 10
           || (undoBytes > 64u * 1024u * 1024u && undoStack.size() > 1))
    {
        undoBytes -= snapshotBytes (*undoStack.front());
        undoStack.pop_front();
    }

    while (redoStack.size() > 10)
        redoStack.pop_front();
}
