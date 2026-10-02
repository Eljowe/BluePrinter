#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// The take recorder: a bounded stack of recorded takes, the review playback
// and the overdub (layered) capture path (tickets 0023 / 0037).
//
//   Idle -> (count-in) -> Recording -> finalize -> addTake -> select/audition
//                                                            -> save/delete
//
// Each retained take owns its audio (a shared_ptr buffer), so takes no longer
// fight over the shared record buffer the looper uses; that buffer is only the
// live capture scratch. The processor owns the record buffer + lock and passes
// them in, so it keeps its existing lock discipline.
//
// Threading: write / renderReview / renderOverdubMonitor run on the audio
// thread and never allocate. The stack (add/replace/select/delete/clear) and
// the review transitions are message-thread only. The audio thread reads the
// selected take through atomics (id / length / playing / position) and an
// a shared audio pointer protected by a non-blocking reader guard; selection
// changes, deletions and clears stop review playback before publication.
// Verified by Tests/test_TakeRecorder.cpp.
class TakeRecorder
{
public:
    enum class State : int
    {
        Idle = 0,
        Recording = 1
    };

    struct TakeView
    {
        int     id     = -1;
        int64_t length = 0;
    };

    static constexpr int    maxTakes     = 8;
    static constexpr size_t maxTakeBytes = 256u * 1024u * 1024u;
    static constexpr size_t maxUndoBytes = 64u * 1024u * 1024u;
    static constexpr size_t maxUndoLayers = 10;

    //==========================================================================
    // Queries (safe on any thread unless noted).
    State   getState()               const { return state.load (std::memory_order_acquire); }
    bool    isRecordingRequested()   const { return recordingRequested.load (std::memory_order_acquire); }
    bool    isPreRollActive()        const { return preRollActive.load (std::memory_order_acquire); }
    bool    isFinalizePending()      const { return finalizePending.load (std::memory_order_acquire); }
    // A take is counting in or capturing.
    bool    isActive()               const { return isRecordingRequested() || isPreRollActive(); }
    int64_t getWritePos()            const { return writePos.load (std::memory_order_acquire); }

    bool    isOverdubEnabled()       const { return overdub.load (std::memory_order_acquire); }
    bool    isOverdubCapture()       const { return overdubCapture.load (std::memory_order_acquire); }
    // A start should layer when the Dub toggle is on and a take is selected.
    bool    wantsOverdub() const;

    // Message thread only.
    int     getTakeCount() const { return static_cast<int> (takes.size()); }
    std::vector<TakeView> getTakeList() const;
    const std::vector<float>& getSelectedTakePeaks() const;
    std::shared_ptr<juce::AudioBuffer<float>> getSelectedTakeAudio() const;
    std::shared_ptr<juce::AudioBuffer<float>> getSelectedTrimmedAudio() const;
    bool    hasSelectedTake() const { return selectedIndex >= 0; }

    int     getSelectedTakeId()     const { return selectedId.load (std::memory_order_acquire); }
    int64_t getSelectedTakeLength() const { return selectedLength.load (std::memory_order_acquire); }
    bool    isReviewPlaying()       const { return reviewPlaying.load (std::memory_order_acquire); }
    int64_t getReviewPos()          const { return reviewPos.load (std::memory_order_acquire); }
    int64_t getTrimStart()          const { return selectedTrimStart.load (std::memory_order_acquire); }
    int64_t getTrimEnd()            const { return selectedTrimEnd.load (std::memory_order_acquire); }
    bool    canUndoOverdub() const; // Message thread.

    //==========================================================================
    // Arming (message thread, except beginFresh/beginOverdub which the
    // processor may call from processBlock with the record lock held).
    void    armCountIn();
    void    beginFresh (juce::AudioBuffer<float>& buffer);
    void    beginOverdub (juce::AudioBuffer<float>& buffer);
    void    endCountIn() { preRollActive.store (false, std::memory_order_release); }
    void    cancelCountIn();
    void    markOverdubPending (bool pending) { overdubPending.store (pending, std::memory_order_release); }
    bool    consumeOverdubPending() { return overdubPending.exchange (false, std::memory_order_acq_rel); }

    //==========================================================================
    // Audio thread.

    // Copies a block at the current write position. When the buffer fills it
    // clears the recording flags and leaves a finalize pending for the message
    // thread. The caller holds the record lock.
    void    write (juce::AudioBuffer<float>& buffer, int maxSamples,
                   const juce::AudioBuffer<float>& source, int numSamples);

    // Overwrites `dest` with the kept region, one-shot. Position and queued
    // seeks use absolute source samples; the trim end is exclusive.
    bool    renderReview (juce::AudioBuffer<float>& dest, int numSamples);

    // Adds the selected take (looping) to `dest` while a layer is captured, so
    // the player hears the take they are playing along to. Monitor-only.
    void    renderOverdubMonitor (juce::AudioBuffer<float>& dest, int numSamples,
                                  int declickSamples);

    //==========================================================================
    // Message-thread transitions.

    // The capture stopped (or the host is tearing down): stop requesting, go
    // idle, and leave a finalize for the finalize path.
    void    finish();
    void    requestFinalize() { finalizePending.store (true, std::memory_order_release); }
    bool    consumeFinalizePending() { return finalizePending.exchange (false, std::memory_order_acq_rel); }
    bool    consumeOverdubCapture() { return overdubCapture.exchange (false, std::memory_order_acq_rel); }

    //==========================================================================
    // Take stack (message thread). Mutating a stack while a capture is in
    // flight is a no-op (the UI disables it too).

    // Publish a freshly captured take; auto-selects it and enforces the bounds.
    void    addTake (std::shared_ptr<juce::AudioBuffer<float>> audio,
                     int64_t length, std::vector<float> peaks);
    // Replace the selected take's audio in place (overdub finalize), retaining
    // the old version for undo (10 snapshots / 64 MB across the take stack).
    void    replaceSelectedTake (std::shared_ptr<juce::AudioBuffer<float>> audio,
                                 int64_t length, std::vector<float> peaks);
    void    selectTake (int id);
    void    deleteTake (int id);
    void    clearTakes();
    bool    setTrim (int64_t start, int64_t end);
    bool    undoOverdub();
    // Takes evicted by the bounds since the last call.
    int     consumeDroppedCount();

    //==========================================================================
    // Review playback.
    bool    canStartReview() const;
    void    startReview (int64_t startSample = -1);
    void    seekReview (int64_t position);
    void    stopReview();

    void    setOverdub (bool enabled) { overdub.store (enabled, std::memory_order_release); }

    void    resetWritePos() { writePos.store (0, std::memory_order_release); }
    void    resetOverdubMonitor() { overdubPlayPos.store (0, std::memory_order_release); }

private:
    struct Take
    {
        int id = -1;
        int64_t length = 0;
        std::vector<float> peaks;
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        int64_t trimStart = 0;
        int64_t trimEnd = 0;
    };

    struct UndoSnapshot
    {
        int takeId = -1;
        int64_t length = 0;
        std::vector<float> peaks;
        std::shared_ptr<juce::AudioBuffer<float>> audio;
    };

    // Readers never wait. Publication waits on the message thread before
    // replacing the selected shared_ptr, avoiding copy-vs-assignment races.
    struct AudioRead
    {
        explicit AudioRead (TakeRecorder& owner) : recorder (owner)
        {
            recorder.audioReaders.fetch_add (1);
            allowed = ! recorder.audioChanging.load();
        }
        ~AudioRead() { recorder.audioReaders.fetch_sub (1); }
        TakeRecorder& recorder;
        bool allowed;
    };

    bool   isMutatingBlocked() const { return isActive() || isOverdubCapture(); }
    size_t totalBytes() const;
    void   refreshSelectedAtomics();
    void   pruneUndoHistory();

    std::vector<Take> takes;
    std::vector<UndoSnapshot> undoHistory;
    int selectedIndex = -1;   // message thread
    int nextTakeId = 1;       // ids are >= 1 so 0 can mean "the selected take"
    int droppedCount = 0;

    std::atomic<State>   state               { State::Idle };
    std::atomic<bool>    recordingRequested  { false };
    std::atomic<bool>    finalizePending     { false };
    std::atomic<bool>    preRollActive       { false };
    std::atomic<int64_t> writePos            { 0 };

    std::atomic<int>     selectedId          { -1 };
    std::atomic<int64_t> selectedLength      { 0 };
    std::atomic<int64_t> selectedTrimStart   { 0 };
    std::atomic<int64_t> selectedTrimEnd     { 0 };
    std::atomic<int>     audioReaders        { 0 };
    std::atomic<bool>    audioChanging       { false };
    std::shared_ptr<juce::AudioBuffer<float>> selectedAudio;
    std::atomic<bool>    reviewPlaying       { false };
    std::atomic<int64_t> reviewPos           { 0 };
    std::atomic<int64_t> pendingReviewSeek   { -1 };

    std::atomic<bool>    overdub             { false };
    std::atomic<bool>    overdubPending      { false };
    std::atomic<bool>    overdubCapture      { false };
    std::atomic<int64_t> overdubPlayPos      { 0 };
};
