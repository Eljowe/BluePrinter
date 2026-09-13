#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>
#include <vector>

// The take recorder's state machine and pending-take review, extracted from
// BluePrinterAudioProcessor (ticket 0027 step 6b).
//
//   Idle -> (count-in) -> Recording -> finalize -> PendingTake -> save/discard
//
// plus the overdub (layered) capture path: a second pass records the new
// input after the take and the processor wrap-mixes it into [0, takeLength)
// on stop. The module owns every take-related atomic and the review peaks;
// the shared capture buffer lives in the processor (the looper uses it too)
// and is passed in, so the caller keeps its existing record-lock discipline.
//
// Threading: write / renderReview / renderOverdubMonitor run on the audio
// thread and never allocate. The begin*/finalize/clear/review methods run on
// the message thread, except beginFresh / beginOverdub / armCountIn, which
// the processor calls from either thread with the lock held where a buffer is
// touched. All flags are atomics so the UI can read them from the message
// thread. Verified by Tests/test_TakeRecorder.cpp.
class TakeRecorder
{
public:
    enum class State : int
    {
        Idle = 0,
        Recording = 1
    };

    //==========================================================================
    // Queries (safe on any thread).
    State   getState()               const { return state.load (std::memory_order_acquire); }
    bool    isRecordingRequested()   const { return recordingRequested.load (std::memory_order_acquire); }
    bool    isPreRollActive()        const { return preRollActive.load (std::memory_order_acquire); }
    bool    isFinalizePending()      const { return finalizePending.load (std::memory_order_acquire); }
    // A take is counting in or capturing.
    bool    isActive()               const { return isRecordingRequested() || isPreRollActive(); }
    int64_t getWritePos()            const { return writePos.load (std::memory_order_acquire); }

    bool    isTakePending()          const { return takePending.load (std::memory_order_acquire); }
    int64_t getTakeLength()          const { return takeLength.load (std::memory_order_acquire); }
    bool    isReviewPlaying()        const { return takePlaybackActive.load (std::memory_order_acquire); }
    int64_t getReviewPos()           const { return takePlaybackPos.load (std::memory_order_acquire); }
    const std::vector<float>& getTakePeaks() const { return takePeaks; }

    bool    isOverdubEnabled()       const { return overdub.load (std::memory_order_acquire); }
    bool    isOverdubCapture()       const { return overdubCapture.load (std::memory_order_acquire); }

    // A start should layer when the Dub toggle is on and there is a pending
    // take to layer onto.
    bool    wantsOverdub() const;

    //==========================================================================
    // Arming (message thread, except beginFresh/beginOverdub which the
    // processor may call from processBlock with the record lock held).
    void    armCountIn();
    // Clears the pending take (a new capture invalidates it) and arms a fresh
    // recording. Clears `buffer` and resets the write position. The caller
    // holds whatever lock protects `buffer`.
    void    beginFresh (juce::AudioBuffer<float>& buffer);
    // Arms a layered capture: keeps the pending take, moves the write position
    // to its length so the layer is captured after it.
    void    beginOverdub();
    void    markOverdubPending (bool pending) { overdubPending.store (pending, std::memory_order_release); }
    bool    consumeOverdubPending() { return overdubPending.exchange (false, std::memory_order_acq_rel); }

    // Cancel a count-in that never captured anything.
    void    cancelCountIn();
    // The count-in finished and the capture is about to begin (keeps the
    // overdub intent so beginActualRecording can pick the layering path).
    void    endCountIn() { preRollActive.store (false, std::memory_order_release); }

    //==========================================================================
    // Audio thread.

    // Copies a block at the current write position. When the buffer fills it
    // clears the recording flags and leaves a finalize pending for the message
    // thread. The caller holds the record lock.
    void    write (juce::AudioBuffer<float>& buffer, int maxSamples,
                   const juce::AudioBuffer<float>& source, int numSamples);

    // Overwrites `dest` with the pending take, one-shot (review playback).
    bool    renderReview (juce::AudioBuffer<float>& dest,
                          const juce::AudioBuffer<float>& buffer, int numSamples);

    // Adds the pending take (looping) to `dest` while a layer is captured, so
    // the player hears the take they are playing along to. Monitor-only.
    void    renderOverdubMonitor (juce::AudioBuffer<float>& dest,
                                  const juce::AudioBuffer<float>& buffer,
                                  int numSamples, int declickSamples);

    //==========================================================================
    // Message thread transitions.

    // The capture stopped (or the host is tearing down): stop requesting, go
    // idle, and leave a finalize for finalizeRecordingOnMessageThread.
    void    finish();
    // Ask for a finalize without stopping (releaseResources).
    void    requestFinalize() { finalizePending.store (true, std::memory_order_release); }
    bool    consumeFinalizePending() { return finalizePending.exchange (false, std::memory_order_acq_rel); }
    bool    consumeOverdubCapture() { return overdubCapture.exchange (false, std::memory_order_acq_rel); }

    // Discard the pending take (atomics + peaks). Message thread only.
    void    clearPending();
    // The fresh take finalized: publish its length and mark it pending.
    void    setPendingTake (int64_t captured);
    // Store the downsampled review waveform (rebuilt on the message thread).
    void    setTakePeaks (std::vector<float> peaks) { takePeaks = std::move (peaks); }

    // Validation + start/stop of the review playback.
    bool    canStartReview() const;
    void    startReview();
    void    stopReview();

    void    setOverdub (bool enabled) { overdub.store (enabled, std::memory_order_release); }

    void    resetWritePos() { writePos.store (0, std::memory_order_release); }
    void    resetOverdubMonitor() { overdubPlayPos.store (0, std::memory_order_release); }

private:
    std::atomic<State>   state               { State::Idle };
    std::atomic<bool>    recordingRequested  { false };
    std::atomic<bool>    finalizePending     { false };
    std::atomic<bool>    preRollActive       { false };
    std::atomic<int64_t> writePos            { 0 };

    std::atomic<bool>    takePending         { false };
    std::atomic<int64_t> takeLength          { 0 };
    std::atomic<bool>    takePlaybackActive  { false };
    std::atomic<int64_t> takePlaybackPos     { 0 };
    std::vector<float>   takePeaks;

    std::atomic<bool>    overdub             { false };
    std::atomic<bool>    overdubPending      { false };
    std::atomic<bool>    overdubCapture      { false };
    std::atomic<int64_t> overdubPlayPos      { 0 };
};
