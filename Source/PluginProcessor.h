/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SnippetLibrary.h"
#include "PluginChain.h"
#include "Vst3Library.h"
#include "KeyDetector.h"

//==============================================================================
/**
*/
class BluePrinterAudioProcessor  : public juce::AudioProcessor, private juce::Timer
{
public:
    //==============================================================================
    BluePrinterAudioProcessor();
    ~BluePrinterAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts {*this, nullptr, "Parameters", createParameterLayout()};

    //==============================================================================
    // Recording / playback / library

    enum class RecordingState : int
    {
        Idle = 0,
        Recording = 1
    };

    void startRecording();
    void stopRecording();
    bool isRecordingRequested() const { return recordingRequested.load (std::memory_order_acquire); }

    void startPlayback (int snippetId);
    void stopPlayback();
    bool isPlaybackActive() const { return playbackActive.load (std::memory_order_acquire); }

    // Pending-take review. After a take stops it is NOT saved
    // automatically — the audio stays in recordBuffer as a pending take
    // so it can be replayed, then explicitly saved to the library or
    // discarded. Any new capture (take or loop) invalidates it.
    bool    isTakePending() const { return takePending.load (std::memory_order_acquire); }
    int64_t getTakeLength() const { return takeLength.load (std::memory_order_acquire); }
    bool    isTakePlaying() const { return takePlaybackActive.load (std::memory_order_acquire); }
    int64_t getTakePlaybackPos() const { return takePlaybackPos.load (std::memory_order_acquire); }
    // Downsampled waveform of the pending take, rebuilt on the message
    // thread when the take finalizes.
    const std::vector<float>& getTakePeaks() const { return takePeaks; }
    void setTakePlayback (bool enabled);
    void savePendingTake();
    void discardPendingTake();

    bool deleteSnippet (int id);
    bool updateSnippetMeta (int id, const juce::String& name, const juce::String& comments);

    // Set the organisational colour tag on a snippet (one of the 8
    // palette keys, or empty to clear). Persists to the sidecar JSON
    // and notifies the UI.
    bool setSnippetColor (int id, const juce::String& color);

    // Run musical-key detection on the snippet's audio. The FFT-based
    // chroma analysis runs on a worker thread; the snippet is updated
    // and the sidecar JSON rewritten on the message thread, then
    // listeners are notified. Replaces any previously detected key.
    void detectSnippetKeyAndNotes (int id);

    SnippetLibrary& getLibrary() { return library; }
    const SnippetLibrary& getLibrary() const { return library; }

    juce::String getLibraryFolder() const;
    void setLibraryFolder (const juce::File& folder);

    // Re-scans the current library folder for any new .wav files and adds
    // them to the in-memory library. Already-loaded files are skipped.
    void refreshLibraryFromFolder();

    // VST3 chains. A flexible list of independent parallel chains runs
    // between the input and the recording tap so captures contain the
    // processed signal:
    //
    //   - Each chain selects which input channels feed it (ChainInputBits
    //     mask: none / left / right / both) and whether its plugins
    //     receive the MIDI buffer ("MIDI" toggle in the chain panel).
    //   - Chains run in parallel: each chain processes its selected
    //     input channels into a scratch buffer, and its output is summed
    //     into the main mix alongside the dry signal.
    //   - Each chain has a "record" toggle. The take recorder and the
    //     looper capture the dry input + the outputs of the selected
    //     chains only, so e.g. a synth chain can be left out of a
    //     guitar take.
    //   - Chains are identified by a stable id ("chain0", "chain1", …)
    //     and a user-editable name, both persisted.
    const std::vector<std::unique_ptr<PluginChain>>& getChains() const { return chains; }
    PluginChain* getChainById (const juce::String& chainId) const;

    // Chain lifecycle (message thread). All of these persist and notify
    // the UI. addChain returns the new chain's id (empty on failure).
    juce::String addChain (const juce::String& name,
                           int inputMask,
                           bool wantsMidi,
                           bool recordOnCapture);
    bool removeChain (const juce::String& chainId);
    bool renameChain (const juce::String& chainId, const juce::String& name);
    bool setChainInputs (const juce::String& chainId, int mask);
    bool setChainRecordOnCapture (const juce::String& chainId, bool enabled);
    // Output volume in dB (-60..+12), mute toggle, and MIDI channel
    // filter (bitmask, bit n = channel n+1). All persist + notify.
    bool setChainVolume (const juce::String& chainId, float volumeDb);
    bool setChainMute (const juce::String& chainId, bool muted);
    bool setChainMidiChannels (const juce::String& chainId, uint16_t mask);

    // Toggle whether a chain's plugins receive the MIDI buffer ("MIDI"
    // toggle in the chain panel). Persists to user state and notifies
    // the UI.
    void setChainWantsMidi (const juce::String& chainId, bool enabled);

    // Folder-wide VST3 metadata shared between both chains: the
    // blocklist of plugins to skip and the cached scan result.
    Vst3Library&       getVst3Library()       { return vst3Library; }
    const Vst3Library& getVst3Library() const { return vst3Library; }

    // Metronome / count-in settings. Persisted in plugin state.
    bool    getMetronomeEnabled() const { return metronomeEnabled.load (std::memory_order_acquire); }
    float   getBpm()              const { return bpm.load (std::memory_order_acquire); }
    int     getCountInBeats()     const { return countInBeats.load (std::memory_order_acquire); }
    int64_t getTransportPosition() const { return transportPosition.load (std::memory_order_acquire); }
    bool    isPreRollActive()     const { return preRollActive.load (std::memory_order_acquire); }

    void setMetronomeEnabled (bool enabled);
    void setBpm (float newBpm);
    void setCountInBeats (int beats);

    // Level of the direct dry pass-through in the output (0..1, 1 =
    // full dry as before). Independent of the chains — turn it down to
    // hear mostly/only what the chains produce, or to zero to silence
    // the dry when all chains are muted. The chains still receive the
    // full input regardless.
    float getDryLevel() const { return dryLevel.load (std::memory_order_acquire); }
    void setDryLevel (float level);

    // Click sound tuning (all message-thread). Stored, re-synthesized
    // immediately, and notified via transportChanged. Persisted in host
    // state with the other metronome settings.
    float getClickPitch()        const { return clickPitch; }
    float getClickAccentPitch()  const { return clickAccentPitch; }
    float getClickDecay()        const { return clickDecay; }
    float getClickVolume()       const { return clickVolume; }
    float getClickAccentVolume() const { return clickAccentVolume; }
    float getClickNoise()        const { return clickNoise; }
    void setClickParams (float pitch, float accentPitch, float decay,
                         float volume, float accentVolume, float noise);

    // Audio looper. Captures the post-chain audio (so synth and FX
    // sounds are baked into the loop) into the shared recordBuffer,
    // then plays it back as an audio-only loop. Optional click +
    // count-in run off the same metronome clock.
    bool    isLooperRecording() const { return audioLoopRecording.load(); }
    bool    isLooperPreRolling() const { return looperPreRollActive.load(); }
    bool    isLooperPlaying() const { return audioLoopPlaying.load(); }
    bool    isLooperLooping() const { return looperLooping.load(); }
    bool    isLooperClickEnabled() const { return looperMetronomeEnabled.load(); }
    int     getLooperCountInBeats() const { return looperCountInBeats.load(); }
    // When false the click only plays during the loop count-in, never
    // through the capture itself. Mirrors clickDuringTake for the take
    // recorder.
    bool    getLooperClickDuringCapture() const { return looperClickDuringCapture.load (std::memory_order_acquire); }
    // Beats trimmed off the start/end of the captured loop (message-thread
    // crop settings, applied to audioLoopStart/audioLoopLength). Beat
    // granularity — finer than the bar-aligned capture trim.
    int     getLooperCropStartBeats() const { return looperCropStartBeats; }
    int     getLooperCropEndBeats() const { return looperCropEndBeats; }
    bool    hasAudioLoop() const { return audioLoopLength.load() > 0; }
    int64_t getAudioLoopPosition() const { return audioLoopPosition.load(); }
    int64_t getAudioLoopLength() const { return audioLoopLength.load(); }
    int64_t getAudioLoopStart() const { return audioLoopStart.load(); }
    // Waveform peaks for the cropped loop region, recomputed on the
    // message thread whenever the loop changes (stop/trim/crop).
    const std::vector<float>& getLooperPeaks() const { return looperPeaks; }
    void    setLooperRecording (bool enabled);
    void    setLooperPlaying (bool enabled);
    void    setLooperLooping (bool enabled);
    void    setLooperClickEnabled (bool enabled);
    void    setLooperCountInBeats (int beats);
    void    setLooperClickDuringCapture (bool enabled);
    // Trim start/end of the loop in whole beats (4 per bar at the current
    // BPM), clamped so the window never fully collapses.
    void    setLoopCrop (int startBeats, int endBeats);
    void    clearLoop();
    // Converts the captured (cropped) loop into a library snippet and
    // writes WAV + JSON to the library folder when one is set — mirrors
    // the take recorder's save (one click, no dialog). Message thread
    // only. Returns the new snippet id, or -1 if there is no captured
    // loop.
    int saveLoopSnippet();

    // MIDI clock output for syncing external hardware (analog drum
    // machines, sequencers). Enabled via the transport UI.
    bool    isMidiClockEnabled()    const { return midiClockEnabled.load (std::memory_order_acquire); }
    void    setMidiClockEnabled (bool enabled);
    juce::String getMidiOutputDeviceName() const;
    void    setMidiOutputDeviceName (const juce::String& name);
    juce::StringArray getAvailableMidiOutputDevices() const;

    // Per-section MIDI clock toggles: the take recorder and the looper can
    // each drive the clock (Start when the operation starts — count-in or
    // capture — Stop when it ends) independently of the free-running global
    // clock toggle. The clock runs while any source wants it.
    bool    getTakeMidiClockEnabled()   const { return takeMidiClockEnabled.load (std::memory_order_acquire); }
    bool    getLooperMidiClockEnabled() const { return looperMidiClockEnabled.load (std::memory_order_acquire); }
    void    setTakeMidiClockEnabled (bool enabled);
    void    setLooperMidiClockEnabled (bool enabled);
    // When false the click only plays during the count-in, never through
    // the take itself. The count-in click itself still plays (pre-roll is
    // independent of this toggle).
    bool    getClickDuringTake() const { return clickDuringTake.load (std::memory_order_acquire); }
    void    setClickDuringTake (bool enabled);

    juce::String getLastSaveError() const;

    // Crash diagnostics (Windows only): the chain-restore step currently
    // running, recorded so the unhandled-exception filter can attribute a
    // crash (e.g. one inside a hosted VST3 DLL) to the exact step. The op
    // is copied into a fixed internal buffer immediately, so any char*
    // passed in may be a temporary.
    static void setCrashOp (const char* op, const char* detail = "");
    static const char* getCrashOp();

    // Set by setStateInformation when the saved VST3 chain couldn't be
    // fully restored (e.g. a plugin's license expired). Read by the UI
    // so the user knows what was skipped. Cleared explicitly.
    juce::String getLastChainRestoreError() const;
    void clearLastChainRestoreError();
    void restoreSavedPluginChains();

    // Arm the debounced plugin-chain bundle save (all chains + the
    // blocklist + the cached scan result). Also wired into every
    // chain's onChanged. Public so the editor can persist non-chain
    // mutations that the bundle carries (e.g. a completed VST3 scan
    // updating availablePlugins, or a blocklist edit).
    void persistPluginChain();

    // True while the deferred chain restore still has work queued
    // (restoreActive, a load in flight, or pending slots on any chain).
    // Message-thread only.
    bool isChainRestoreInProgress() const;

    // Meter values updated by the audio thread (peak + RMS over the last block).
    float getCurrentInputLevel() const { return inputLevel.load (std::memory_order_acquire); }
    float getCurrentInputPeak() const  { return inputPeak.load  (std::memory_order_acquire); }

    // Snippet currently being captured (only valid while recordingRequested is true).
    int getRecordingLengthSamples() const { return static_cast<int> (recordWritePos.load (std::memory_order_acquire)); }

    // Snippet currently being played back (-1 if none).
    int getPlayingSnippetId() const { return playingSnippetId.load (std::memory_order_acquire); }
    int getPlaybackPositionSamples() const { return static_cast<int> (playbackReadPos.load (std::memory_order_acquire)); }

    // Notification hook used by the editor so the UI can refresh on demand.
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void libraryChanged() {}
        virtual void transportChanged() {}
        virtual void pluginChainChanged() {}
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    static constexpr int maxRecordingSeconds = 120;
    static constexpr int transportTimerHz    = 30;
    static constexpr int levelSmoothing      = 8;

private:
    void timerCallback();
    void finalizeRecordingOnMessageThread();
    void beginActualRecording();

    void writeRecording (const juce::AudioBuffer<float>& source, int numSamples);
    void renderPlayback (juce::AudioBuffer<float>& destination, int numSamples);
    void renderTakePlayback (juce::AudioBuffer<float>& destination, int numSamples);
    void computeLevels  (const juce::AudioBuffer<float>& source, int numSamples);
    // Level-metering core, shared by the main input meter and the
    // per-chain meters. gain scales the meter to reflect the chain's
    // output volume.
    void computeLevelsInto (const juce::AudioBuffer<float>& source,
                            int numSamples,
                            std::atomic<float>& levelAtomic,
                            std::atomic<float>& peakAtomic,
                            float gain);
    void renderMetronomeInBlock (juce::AudioBuffer<float>& buffer, int64_t startPos, int numSamples);
    // Recomputes whether any source wants the MIDI clock running and
    // sends Start/Stop on the edges. Message thread only (may open/close
    // the output device). refreshClockRunning is the audio-thread-safe
    // version: it only updates the running flag and commands the
    // transport when an operation it owns ends on the audio thread
    // (one-shot loop finished, max-length recording filled).
    void updateClockRunState();
    void refreshClockRunning();

    juce::ListenerList<Listener> listeners;

    SnippetLibrary library;
    Vst3Library    vst3Library;
    // The chain list. Chains are independent parallel processors of the
    // input; see the chain API comments above. Owned by the processor
    // (unique_ptr), guarded by chainLock. The audio thread iterates a
    // raw-pointer snapshot (blockChains) taken under chainLock at the
    // start of each block — same lifetime model PluginChain uses for
    // its slots.
    std::vector<std::unique_ptr<PluginChain>> chains;
    mutable juce::CriticalSection chainLock;
    // Counter for generating stable chain ids. Persisted with the chain
    // state so ids never collide after a restore.
    int nextChainId = 0;

    // Pre-allocated record buffer. Allocated on the message thread inside
    // prepareToPlay, written to from the audio thread — no allocations there.
    std::unique_ptr<juce::AudioBuffer<float>> recordBuffer;
    int maxRecordSamples = 0;
    juce::CriticalSection recordLock;

    // Per-block scratch buffers, sized in prepareToPlay.
    // chainInputBuffer is a pristine copy of the post-gain input taken
    // once per block — every chain copies its selected channels from
    // HERE, never from the accumulating mix, so a chain can't process
    // another chain's output. chainScratchBuffer holds the selected
    // input channels for the chain currently running; its output is
    // summed into the main buffer (× volume, unless muted) and, if the
    // chain is selected for capture, into recordingMixBuffer.
    // recordingMixBuffer starts as the dry post-gain input, so captures
    // contain dry + selected chains — identical to the old single-
    // record-buffer tap when every chain is selected.
    juce::AudioBuffer<float> chainInputBuffer;
    juce::AudioBuffer<float> chainScratchBuffer;
    juce::AudioBuffer<float> recordingMixBuffer;
    juce::MidiBuffer chainMidiScratch;
    // Scratch for the per-chain MIDI channel filter. Member so the
    // audio thread never allocates.
    juce::MidiBuffer chainMidiFiltered;
    // Audio-thread snapshot of the chain pointers for this block.
    // Reused member so no allocation happens in processBlock.
    std::vector<PluginChain*> blockChains;

    std::atomic<RecordingState> recordingState { RecordingState::Idle };
    std::atomic<bool> recordingRequested { false };
    std::atomic<bool> recordingFinalizePending { false };
    std::atomic<int64_t> recordWritePos { 0 };

    // Pending-take review state. After a take stops, its audio stays in
    // recordBuffer until the user saves it to the library or discards
    // it. takePending/takeLength are set on the message thread when the
    // take finalizes; takePlaybackActive/Pos drive the review playback
    // on the audio thread. takePeaks is message-thread only.
    std::atomic<bool>    takePending        { false };
    std::atomic<int64_t> takeLength         { 0 };
    std::atomic<bool>    takePlaybackActive { false };
    std::atomic<int64_t> takePlaybackPos    { 0 };
    std::vector<float>   takePeaks;

    void refreshTakePeaks();
    // Clears the pending-take state. Message thread only (touches the
    // takePeaks vector). Audio-thread invalidation (beginActualRecording)
    // clears just the atomics.
    void clearPendingTake();

    std::atomic<bool> playbackActive { false };
    std::atomic<int> playingSnippetId { -1 };
    std::atomic<int64_t> playbackReadPos { 0 };

    // Metronome / count-in. Settings are user-tweakable and persisted; the
    // preRoll* / transportPosition fields are audio-thread runtime state.
    std::atomic<bool>    metronomeEnabled { true };
    std::atomic<bool>    clickDuringTake  { true };
    std::atomic<float>   bpm              { 120.0f };
    std::atomic<int>     countInBeats     { 4 };
    std::atomic<float>   dryLevel         { 1.0f };
    std::atomic<bool>    preRollActive    { false };
    std::atomic<int64_t> transportPosition { 0 };
    std::atomic<int64_t> metronomePosition { 0 };

    // Audio looper state. Capture writes the post-chain audio into
    // recordBuffer up to audioLoopLength; crop skips audioLoopStart
    // samples at playback/save time. All audio-thread reads go through
    // the atomics; the crop bar counts are message-thread only.
    std::atomic<bool>    looperMetronomeEnabled { true };
    std::atomic<bool>    looperClickDuringCapture { true };
    std::atomic<int>     looperCountInBeats     { 4 };
    std::atomic<bool>    looperPreRollActive    { false };
    std::atomic<bool>    looperCaptureArmed     { false };
    std::atomic<bool>    looperLooping          { true };
    std::atomic<int64_t> audioLoopStart   { 0 };
    std::atomic<int64_t> audioLoopLength  { 0 };
    std::atomic<int64_t> audioLoopPosition { 0 };
    std::atomic<bool> audioLoopRecording { false };
    std::atomic<bool> audioLoopPlaying   { false };
    int looperCropStartBeats = 0;
    int looperCropEndBeats   = 0;
    int loopCrossfadeSamples = 0;
    // Message-thread only: downsampled waveform of the cropped loop,
    // rebuilt by refreshLooperPeaks() after capture/trim/crop.
    std::vector<float> looperPeaks;

    void refreshLooperPeaks();
    void trimLooperToMusicalGrid();

    // Pre-rendered metronome clicks. Two sounds, both synthesized by
    // resynthesizeClicks() (message thread only): a bright accent tick
    // for the first beat of each bar (accentClickBuffer) and a softer
    // tick for the other beats (clickBuffer). Each is a short
    // percussive burst — 2 ms attack ramp (no pop), fast exponential
    // decay, tail fade, and a tiny noise transient at the onset for
    // the woodblock "tick" character. Held as shared_ptr so the audio
    // thread can render while the message thread swaps in new buffers
    // after a parameter change.
    std::shared_ptr<const std::vector<float>> clickBuffer;
    std::shared_ptr<const std::vector<float>> accentClickBuffer;

    // Click sound parameters (message-thread only). Persisted in host
    // state like the other metronome settings. Tuned via the "Click
    // sound" popup in the transport.
    float clickPitch        = 1000.0f;  // normal tick frequency (Hz)
    float clickAccentPitch  = 1500.0f;  // bar-first-beat frequency (Hz)
    float clickDecay        = 90.0f;    // exponential decay rate (/s)
    float clickVolume       = 0.35f;    // normal tick level
    float clickAccentVolume = 0.50f;    // accent tick level
    float clickNoise        = 0.10f;    // onset transient level
    void resynthesizeClicks();
    double currentSampleRate = 44100.0;

    // MIDI clock output. Clock pulses (0xF8, 24 ppqn) are generated in
    // processBlock alongside the audible metronome. MIDI Start / Stop
    // are queued from the message thread and flushed at the start of
    // the next audio block. The clock runs whenever any source wants it:
    // the free-running global toggle (midiClockEnabled), the take
    // recorder (takeMidiClockEnabled while a take or its count-in is
    // active), or the looper (looperMidiClockEnabled while pre-rolling,
    // capturing or playing).
    std::atomic<bool>    midiClockEnabled       { false };
    std::atomic<bool>    takeMidiClockEnabled   { false };
    std::atomic<bool>    looperMidiClockEnabled { false };
    std::atomic<bool>    clockRunning           { false };
    std::atomic<bool>    midiStartPending       { false };
    std::atomic<bool>    midiStopPending        { false };
    juce::CriticalSection midiOutputLock;
    juce::String         midiOutputDeviceName;
    std::unique_ptr<juce::MidiOutput> midiOutput;

    void renderMidiClockInBlock (juce::MidiBuffer& midiMessages, int64_t metronomePos, int numSamples);
    void openMidiOutputDevice();
    void closeMidiOutputDevice();

    // Cached audio-thread copies. Updated under the library lock briefly,
    // then held as shared_ptrs so playback can't dangle.
    std::shared_ptr<const Snippet> playbackSnippet;

    std::atomic<float> inputLevel { 0.0f };
    std::atomic<float> inputPeak  { 0.0f };

    juce::File libraryFolder;
    juce::CriticalSection libraryFolderLock;
    juce::String lastSaveError;

    // User-state persistence for the standalone build. Standalone
    // never has a host calling getStateInformation/setStateInformation
    // automatically, so without this the library folder and VST3
    // chain are lost on every restart. The file lives under
    // userApplicationDataDirectory/Retrokielto/BluePrinter.properties
    // (%APPDATA%/Retrokielto/BluePrinter.properties on Windows).
    std::unique_ptr<juce::PropertiesFile> userState;

    // Lazily open the properties file. Returns nullptr on disk failure
    // (read-only volume, missing perms) so the rest of the code can
    // keep working in-memory without crashing.
    juce::PropertiesFile* getUserState();

    // One-shot restore from userState: sets the library folder (which
    // also auto-loads snippets) and replays the saved VST3 chain.
    // Safe to call on every construction; no-op if userState is empty.
    void restoreUserState();

    // Write the library folder string to userState. Called from
    // setLibraryFolder.
    void persistLibraryFolder();

    // Serialise the VST3 chain to userState. Wired into
    // pluginChain.onChanged so it runs after every add/remove/
    // bypass/move. Skipped while a restore is in progress to avoid
    // writing the just-loaded state back over the file. Debounced:
    // the call only marks a pending save, and the actual (expensive —
    // it serializes every plugin's state) serialize + disk write runs
    // from timerCallback 500 ms after the last change. That keeps
    // high-frequency mutations (dragging a chain volume knob) from
    // stalling the message thread. flushPendingChainPersist runs the
    // pending save immediately (also called on release/destruction so
    // the final state is never lost).
    void flushPendingChainPersist();
    bool persistingPluginChain = false;
    bool pluginChainsRestored = false;
    bool chainPersistPending = false;
    int64_t chainPersistDeadline = 0;
    // One async plugin load in flight from the deferred restore driver
    // (timerCallback); message-thread only.
    std::atomic<int> pendingPluginLoads { 0 };
    // Set while applyChainState is running; the driver must not start a
    // load during a restore (clearChains would destroy the chain that a
    // worker thread is about to finalize into).
    bool restoreActive = false;

    // Build the combined plugin-chain bundle (all chains + library
    // metadata) for persistence. See PluginProcessor.cpp for the
    // exact format.
    juce::var makeChainState() const;

    // Inverse of makeChainState. Accepts the new chains-array format,
    // the midiChain/audioChain-keyed split format, and the pre-split
    // single-chain format (the latter two are migrated to chains with
    // the old behaviour preserved). Suppresses chain persistence for
    // its whole duration so a restore can never echo a partial state
    // back into the properties file.
    void applyChainState (const juce::var& state, juce::String& outError);

    // Read the saved chain bundle from user state, preferring whichever
    // of the "pluginChains"/"pluginChain" keys actually holds chain
    // content (so a stale or corrupted newer key — e.g. an empty chains
    // array written by an old restore echo — can't shadow the older
    // valid one). Returns a void var when nothing usable is saved.
    juce::var loadSavedChainState();

    // Create a chain with the given config and push it into the list.
    // Does NOT persist or notify — the caller decides (used during
    // restore with persistingPluginChain set).
    PluginChain* createChain (const juce::String& name,
                              int inputMask,
                              bool wantsMidi,
                              bool recordOnCapture);

    // Drop every chain (restore path). Fires onSlotRemoved so the
    // editor can close any open plugin windows.
    void clearChains();

    // Reassign fresh ids to any chain whose id is missing or duplicated,
    // and bump nextChainId past the highest id in use. Called after
    // applyChainState.
    void ensureUniqueChainIds();

    // Stashed when setStateInformation fails to restore one or more
    // chain plugins (e.g. expired-license VST3s). Read by the UI on
    // open so the user knows what was skipped.
    juce::String lastChainRestoreError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BluePrinterAudioProcessor)
};
