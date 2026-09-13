#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "LooperGridMath.h"

// The audio looper's state, extracted from BluePrinterAudioProcessor (ticket
// 0027 step 6c): capture arming/recording, the loop descriptor (start /
// length / full length / playhead), the whole-beat crop, fixed-length capture,
// grid trim, the loop-layer undo/redo history and the waveform peaks.
//
// The shared capture buffer (recordBuffer) and its record lock live in the
// processor (the take recorder uses them too), so buffer-touching operations
// take the buffer by reference and the caller keeps its existing lock
// discipline. The loop-level gain, the crossfade length, the click/metronome
// clock and the loop meter stay in the processor as orchestration; the pure
// wrap-mix DSP stays in LoopPlayback and the crop/trim math in LooperGridMath.
//
// Threading: captureBlock / renderPlayback / beginCapture run on the audio
// thread and never allocate. arm*/stop*/trim/crop/history/clear run on the
// message thread; pushUndoSnapshot / undo / redo copy or write the buffer and
// must be called with the record lock held (they allocate on the message
// thread only). Verified by Tests/test_Looper.cpp.
class Looper
{
public:
    //==========================================================================
    // Queries (safe on any thread).
    bool    isRecording()        const { return recording.load (std::memory_order_acquire); }
    bool    isPreRollActive()    const { return preRollActive.load (std::memory_order_acquire); }
    bool    isCaptureArmed()     const { return captureArmed.load (std::memory_order_acquire); }
    bool    isAutoStopPending()  const { return autoStopPending.load (std::memory_order_acquire); }
    // Counting in, capturing, or armed to capture.
    bool    isActive()           const { return isCaptureArmed() || isPreRollActive(); }
    bool    isPlaying()          const { return playing.load (std::memory_order_acquire); }
    bool    isLooping()          const { return looping.load (std::memory_order_acquire); }
    bool    isOverdub()          const { return overdub.load (std::memory_order_acquire); }
    bool    isOverdubCapture()   const { return overdubCapture.load (std::memory_order_acquire); }
    bool    hasLoop()            const { return length.load (std::memory_order_acquire) > 0; }
    bool    isPlaybackReverse()  const { return playbackReverse.load (std::memory_order_acquire); }
    bool    isPlaybackHalfSpeed()const { return playbackHalfSpeed.load (std::memory_order_acquire); }

    int     getCountInBeats()    const { return countInBeats.load (std::memory_order_acquire); }
    int     getLengthBars()      const { return lengthBars.load (std::memory_order_acquire); }
    int     getCropStartBeats()  const { return cropStartBeats; }
    int     getCropEndBeats()    const { return cropEndBeats; }

    int64_t getStart()           const { return start.load (std::memory_order_acquire); }
    int64_t getLength()          const { return length.load (std::memory_order_acquire); }
    int64_t getFullLength()      const { return fullLength.load (std::memory_order_acquire); }
    int64_t getOverdubWritePos() const { return overdubWritePos.load (std::memory_order_acquire); }
    double  getPosition()        const { return position.load (std::memory_order_acquire); }

    const std::vector<float>& getPeaks() const { return peaks; }

    // Playback/capture all idle (the history edit guard).
    bool    isIdle() const;

    //==========================================================================
    // Audio thread.

    // Writes a block of the recording mix into the loop while armed: a fresh
    // capture appends at `length`, an overdub layer at `overdubWritePos` (so
    // the fixed loop length never moves mid-capture). A fresh capture that
    // reaches `targetSamples` (> 0) stops itself and leaves an auto-stop
    // pending for the message thread.
    void    captureBlock (juce::AudioBuffer<float>& buffer, int maxSamples,
                          const juce::AudioBuffer<float>& source, int numSamples,
                          int64_t targetSamples);

    // Adds the playing loop to `dest`. `gain` is the loop-level gain,
    // `declickSamples` the seam fade base. Sets `outBlockPeak` to the peak
    // added this block. Returns false when there is nothing to play.
    bool    renderPlayback (juce::AudioBuffer<float>& dest,
                            const juce::AudioBuffer<float>& buffer,
                            int numSamples, float gain, int declickSamples,
                            float& outBlockPeak);

    // The count-in finished: arm the capture. A fresh capture resets the loop
    // length; an overdub keeps it and starts the loop from the top so the
    // layer aligns with the downbeat.
    void    beginCapture();

    //==========================================================================
    // Message-thread arming / stopping.
    void    armFresh();
    void    armOverdub (bool countIn);
    void    armCountIn() { preRollActive.store (true, std::memory_order_release); }
    void    armCaptureNow();
    // Common stop flags (keeps overdubCapture for the caller to consume).
    void    stopCapture();
    void    stopPlaying() { playing.store (false, std::memory_order_release); }
    bool    consumeOverdubCapture() { return overdubCapture.exchange (false, std::memory_order_acq_rel); }
    bool    consumeAutoStopPending() { return autoStopPending.exchange (false, std::memory_order_acq_rel); }

    //==========================================================================
    // Message-thread edits.
    // Publish a fresh grid-trimmed capture: start 0, length/full = target,
    // crop cleared.
    void    setGridTrimmed (int64_t target);
    // Whole-beat crop against the full loop. Returns false when there is no
    // loop (the caller then skips its notify).
    bool    setCrop (int startBeats, int endBeats, double sampleRate, float bpm,
                     LooperGrid::TimeSignature meter);
    void    setPlaying (bool enabled);
    void    setLooping (bool enabled) { looping.store (enabled, std::memory_order_release); }
    void    setOverdub (bool enabled);
    void    setCountInBeats (int beats) { countInBeats.store (juce::jlimit (0, 8, beats)); }
    void    setLengthBars (int bars) { lengthBars.store (juce::jlimit (0, 16, bars), std::memory_order_release); }
    void    setLengthBarsValue (int bars) { lengthBars.store (bars, std::memory_order_release); }
    void    setPlaybackReverse (bool enabled) { playbackReverse.store (enabled, std::memory_order_release); }
    void    setPlaybackHalfSpeed (bool enabled) { playbackHalfSpeed.store (enabled, std::memory_order_release); }
    void    setPeaks (std::vector<float> newPeaks) { peaks = std::move (newPeaks); }

    // Wipe the loop and its history.
    void    clear();

    //==========================================================================
    // Layer undo/redo (0030). The caller holds the record lock.
    void    pushUndoSnapshot (const juce::AudioBuffer<float>& buffer);
    bool    undo (juce::AudioBuffer<float>& buffer);
    bool    redo (juce::AudioBuffer<float>& buffer);
    bool    isUndoAvailable() const { return ! undoStack.empty(); }
    bool    isRedoAvailable() const { return ! redoStack.empty(); }
    void    clearHistory();

private:
    void    applySnapshot (juce::AudioBuffer<float>& buffer,
                           const std::shared_ptr<juce::AudioBuffer<float>>& snapshot);
    void    trimHistoryStacks();

    std::atomic<int>     countInBeats    { 4 };
    std::atomic<int>     lengthBars      { 0 };
    std::atomic<bool>    autoStopPending { false };
    std::atomic<bool>    preRollActive   { false };
    std::atomic<bool>    captureArmed    { false };
    std::atomic<bool>    looping         { true };
    std::atomic<bool>    overdub         { false };
    std::atomic<bool>    overdubCapture  { false };
    std::atomic<int64_t> overdubWritePos { 0 };

    std::atomic<int64_t> start           { 0 };
    std::atomic<int64_t> length          { 0 };
    std::atomic<double>  position        { 0.0 };
    std::atomic<int64_t> fullLength      { 0 };
    std::atomic<bool>    recording       { false };
    std::atomic<bool>    playing         { false };
    std::atomic<bool>    playbackReverse   { false };
    std::atomic<bool>    playbackHalfSpeed { false };

    int cropStartBeats = 0;
    int cropEndBeats   = 0;
    std::vector<float> peaks;

    std::deque<std::shared_ptr<juce::AudioBuffer<float>>> undoStack;
    std::deque<std::shared_ptr<juce::AudioBuffer<float>>> redoStack;
    size_t undoBytes = 0;
};
