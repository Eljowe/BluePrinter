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
#include "MetronomePlayer.h"
#include "MidiClockOutput.h"
#include "Meter.h"
#include "LooperGridMath.h"
#include "TapTempo.h"
#include "TakeRecorder.h"
#include "MelodyAnalyzer.h"
#include "MelodyPlayer.h"
#include "Looper.h"
#include "StemCapture.h"
#include "SetlistStore.h"
#include "RestoreSelfHeal.h"
#include "ChainStatePersistence.h"
#include <deque>
#include <map>
#include <set>

//==============================================================================
// A dedicated thread that owns every VST3 instantiation so every
// plugin's windows are created on — and pumped by — ONE thread that
// belongs to the plugins, never to the app's main message loop.
// Third-party factories create windows at instantiation time, and a
// window created from a pump-less worker thread is a window nobody
// pumps; its messages ended up dispatched reentrantly by OUR message
// loop, which is how a second Neural DSP "X" instance died with a
// -1-pointer deref inside its own window code while still inside its
// factory. On the apartment, plugin windows only ever pump here, the
// main loop never sees them, and the plugins get a stable
// Ableton-style UI home. See getPluginUiApartment().
class PluginUiApartment : private juce::Thread
{
public:
    PluginUiApartment();
    ~PluginUiApartment() override;

    // Queue fn to run on the apartment thread. Non-blocking; results
    // travel back through whatever the caller captured. Tasks run in
    // FIFO order and never interleave with the apartment's window
    // pump (messages queue up and are dispatched only after a task
    // returns), so no window-message reentrancy can happen while a
    // plugin factory is executing.
    void post (std::function<void()> fn);

private:
    void run() override;

    juce::CriticalSection tasksLock;
    std::deque<std::function<void()>> tasks;
    std::atomic<bool> queueReady { false };
};

// Process-wide singleton: ALL plugin instantiations (chain loads AND
// the VST3 folder scanner) must go through here.
PluginUiApartment& getPluginUiApartment();

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

    void startRecording();
    void stopRecording();
    bool isRecordingRequested() const { return takeRecorder.isRecordingRequested(); }

    // `startSample` optionally starts the snippet partway through (clamped
    // by the audio thread) so the UI can click a waveform to play from there.
    void startPlayback (int snippetId, int startSample = 0);
    void stopPlayback();
    // Jump the currently-playing snippet to `sample` (message thread; the
    // audio thread applies it next block). No-op when nothing is playing.
    void setPlaybackPosition (int sample);
    bool isPlaybackActive() const { return playbackActive.load (std::memory_order_acquire); }

    // Take review. Every stopped take is retained in a bounded stack (0037)
    // and is NOT saved automatically: each can be auditioned, then explicitly
    // saved to the library or deleted. The stack is session-only.
    bool    isTakePending() const { return takeRecorder.getTakeCount() > 0; }
    int64_t getTakeLength() const { return takeRecorder.getSelectedTakeLength(); }
    bool    isTakePlaying() const { return takeRecorder.isReviewPlaying(); }
    int64_t getTakePlaybackPos() const { return takeRecorder.getReviewPos(); }
    // Downsampled waveform of the selected take, rebuilt on the message
    // thread when the take finalizes.
    const std::vector<float>& getTakePeaks() const { return takeRecorder.getSelectedTakePeaks(); }
    int     getSelectedTakeId() const { return takeRecorder.getSelectedTakeId(); }
    std::vector<TakeRecorder::TakeView> getTakeList() const { return takeRecorder.getTakeList(); }
    void setTakePlayback (bool enabled);
    void savePendingTake();
    void discardPendingTake();
    // Id-targeted actions for the per-take UI (id <= 0 = the selected take).
    void saveTake (int id);
    void discardTake (int id);
    void selectTake (int id);
    void discardAllTakes();
    // Takes evicted by the stack bounds since the last call (for the UI).
    int consumeTakesDropped() { return takeRecorder.consumeDroppedCount(); }

    // Take-recorder overdub (session-only, like the looper's switch): with
    // a take selected, the next record layers the new input over it instead
    // of adding a new take. The take keeps playing while you play along,
    // and the new layer is wrapped-mixed into it on stop.
    bool isTakeOverdub() const { return takeRecorder.isOverdubEnabled(); }
    void setTakeOverdub (bool enabled);

    // Melody extraction (0054). Analysis runs off the message thread on the
    // selected take's audio; the result is cached per take id (session-only)
    // and can be auditioned with the built-in synth. Saving a take copies the
    // cached melody into the snippet's sidecar JSON.
    void    analyzeTakeMelody (int id);
    void    setMelodyPlayback (bool enabled, int64_t startSample = -1);
    bool    isMelodyPlaying() const { return melodyPlayer.isPlaying(); }
    int64_t getMelodyPlaybackPos() const { return melodyPlayer.getPosition(); }
    int64_t getMelodyLength() const { return melodyPlayer.getLength(); }
    bool    isMelodyAnalysing() const { return melodyAnalysing.load (std::memory_order_acquire); }
    int     getMelodyAnalysingTakeId() const { return melodyAnalysingTakeId.load (std::memory_order_acquire); }
    // The cached melody for a take (nullptr when none has been analysed).
    // Message thread only.
    const MelodyAnalyzer::Result* getTakeMelody (int id) const;

    // Library-snippet melody (0054 follow-up). Analyses a snippet already in
    // the library off the message thread and persists the notes into its
    // sidecar; the card can then draw the piano-roll and audition it. A
    // re-analysis supersedes an in-flight one. `isSnippetMelodyAnalysing`
    // drives the card's spinner; `getMelodyPlayingSource`/`getMelodyPlayingId`
    // tell the take review and the cards which melody is currently sounding.
    void         analyzeSnippetMelody (int id);
    void         setSnippetMelodyPlayback (int id, bool enabled, int64_t startSample = -1);
    bool         isSnippetMelodyAnalysing (int id) const;
    juce::String getMelodyPlayingSource() const;
    int          getMelodyPlayingId() const;

    bool deleteSnippet (int id);
    bool updateSnippetMeta (int id, const juce::String& name, const juce::String& comments);

    // Set the organisational colour tag on a snippet (one of the 8
    // palette keys, or empty to clear). Persists to the sidecar JSON
    // and notifies the UI.
    bool setSnippetColor (int id, const juce::String& color);

    // Set the user favourite (star) flag on a snippet. Persists to the
    // sidecar JSON and notifies the UI.
    bool setSnippetFavourite (int id, bool favourite);

    // Bulk operations on a selection (0035). Each persists the affected
    // sidecars once and fires a single libraryChanged; they return the
    // number of snippets actually changed. Message thread only.
    int setSnippetsColor (const std::vector<int>& ids, const juce::String& color);
    int deleteSnippets (const std::vector<int>& ids);
    int addSnippetsToSetlist (const juce::String& setlistId, const std::vector<int>& ids);

    // Non-destructive playback trim (dB, -24..+24) for one snippet.
    // Persists to the sidecar JSON and notifies the UI.
    bool setSnippetGain (int id, float gainDb);
    // Set the trim so the snippet's peak lands at -1 dBFS (clamped to
    // -24..+24). No-op if the audio is empty/silent.
    bool normalizeSnippet (int id);

    // Export a snippet to an arbitrary file, choosing the format from the
    // file's extension. `applyGain` bakes the non-destructive gainDb trim
    // into the exported audio (the library's own save never does). Returns
    // false and sets outError on failure.
    bool exportSnippetToFile (int id, const juce::File& file, bool applyGain, juce::String& outError);

    // Extensions this build can export to (.wav, .aiff, .flac).
    juce::StringArray getSupportedExportExtensions() const;

    // Import an external audio file (FileChooser) or dropped audio bytes
    // (base64, drag-drop — the WebView exposes no path) into the library.
    // Returns the new snippet id, or -1 with outError set; pushes a
    // library snapshot on success.
    int importAudioFile (const juce::File& file, juce::String& outError);
    int importAudioFromBase64 (const juce::String& name,
                               const juce::String& base64,
                               juce::String& outError);

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
    // Monitor-only solo/mute: change what is heard, never the capture.
    bool setChainMonitorSolo (const juce::String& chainId, bool solo);
    bool setChainMonitorMute (const juce::String& chainId, bool muted);
    bool setChainMidiChannels (const juce::String& chainId, uint16_t mask);

    // Toggle whether a chain's plugins receive the MIDI buffer ("MIDI"
    // toggle in the chain panel). Persists to user state and notifies
    // the UI.
    void setChainWantsMidi (const juce::String& chainId, bool enabled);

    // Named chain presets (0033). One JSON file per preset under
    // %APPDATA%/Retrokielto/chain-presets; the payload is the chain's rig
    // only (see ChainPreset.h). Message-thread only.
    // Snapshot shape: [{ name, file }] where `file` is the stable id.
    juce::var getChainPresetsSnapshot() const;
    juce::File getChainPresetsFolder() const;
    // Save the chain's rig as a named preset. When a preset of that file
    // name already exists and overwrite is false, returns false with
    // outError == "exists" so the caller can ask for confirmation. Refused
    // while the chain still has pending (restoring) slots.
    bool saveChainPreset (const juce::String& chainId, const juce::String& presetName,
                          bool overwrite, juce::String& outError);
    // Load a preset into a chain, replacing its slots via the deferred
    // restore driver. Returns false (outError set) when the target chain is
    // busy or the document is invalid. outWarning reports non-fatal problems
    // (unknown fields, skipped/missing/quarantined plugins) while the rest
    // still load.
    bool loadChainPreset (const juce::String& chainId, const juce::String& file,
                          juce::String& outError, juce::String& outWarning);
    // Rename / delete a preset by its file stem. Rename overwrites an
    // existing target only when `overwrite` is true (else outError ==
    // "exists").
    bool renameChainPreset (const juce::String& file, const juce::String& newName,
                            bool overwrite, juce::String& outError);
    bool deleteChainPreset (const juce::String& file, juce::String& outError);

    // Folder-wide VST3 metadata shared between both chains: the
    // blocklist of plugins to skip and the cached scan result.
    Vst3Library&       getVst3Library()       { return vst3Library; }
    const Vst3Library& getVst3Library() const { return vst3Library; }

    // Metronome / count-in settings. Persisted in plugin state.
    bool    getMetronomeEnabled() const { return metronomeEnabled.load (std::memory_order_acquire); }
    float   getBpm()              const { return bpm.load (std::memory_order_acquire); }
    int     getCountInBeats()     const { return countInBeats.load (std::memory_order_acquire); }
    // Notated meter (numerator/denominator); default 4/4. BPM is a
    // quarter-note tempo, so an eighth beat (6/8) is half a quarter.
    int     getTimeSignatureNumerator()   const { return timeSignatureNumerator.load (std::memory_order_acquire); }
    int     getTimeSignatureDenominator() const { return timeSignatureDenominator.load (std::memory_order_acquire); }
    // Built-in tuner (0034). The reading is published by a worker thread that
    // runs only while the popover is open; frequency is 0 when there is no
    // confident pitch.
    bool  isTunerOpen() const { return tunerOpen.load (std::memory_order_acquire); }
    bool  isTunerMonitorMuted() const { return tunerMonitorMute.load (std::memory_order_acquire); }
    float getTunerFrequency() const { return tunerFrequency.load (std::memory_order_acquire); }
    float getTunerConfidence() const { return tunerConfidence.load (std::memory_order_acquire); }
    float getTunerReferencePitch() const { return tunerReferencePitch.load (std::memory_order_acquire); }
    int64_t getTransportPosition() const { return transportPosition.load (std::memory_order_acquire); }
    bool    isPreRollActive()     const { return takeRecorder.isPreRollActive(); }

    void setMetronomeEnabled (bool enabled);
    void setBpm (float newBpm);
    // Tap tempo (0049): register a tap at `nowMs` (non-decreasing milliseconds,
    // message thread). Ignored while a take or loop capture is active; otherwise
    // sets the BPM once enough taps have been averaged.
    void registerTapTempo (int64_t nowMs);
    void setCountInBeats (int beats);
    // Set the notated meter (numerator/denominator), clamped to the supported
    // values. Affects future captures, the grid trim and the click accents.
    void setTimeSignature (int numerator, int denominator);
    // Built-in tuner (0034). open starts/stops the analysis worker and is
    // persisted; monitor mute is session-only and never affects the capture.
    void setTunerOpen (bool open);
    void setTunerMonitorMute (bool muted);
    void setTunerReferencePitch (float hz);

    // Monitor-only playback level for the looper (dB, -60..+12, 0 =
    // unity). Scales the loop playback without touching the capture or
    // the record bus, so a loud loop can be pulled down during an
    // overdub to hear the new layer.
    float getLoopLevel() const { return loopLevel.load (std::memory_order_acquire); }
    void setLoopLevel (float levelDb);

    // Direct dry pass-through level (dB, -60..0, 0 = unity). Scales the
    // dry signal in both the monitor mix and the capture; the chains
    // always receive the full input, so at -60 dB only the chains are
    // heard and printed.
    float getDryLevel() const { return dryLevel.load (std::memory_order_acquire); }
    void setDryLevel (float levelDb);

    // Overdub trim (dB, -60..0, 0 = unity) applied by mixOverdubLayer when
    // each new layer is summed into the loop, so repeated layers can be
    // attenuated before they pile up. Monitor/capture-neutral: it only
    // scales the layer before the sum.
    float getOverdubLevel() const { return overdubLevel.load (std::memory_order_acquire); }
    void setOverdubLevel (float levelDb);

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

    // Click rhythm (0050): subdivisions per beat (0 = off, 2/3/4 =
    // eighth/triplet/sixteenth) and a per-beat accent mask, bit (beat index
    // mod numerator). Both audio-thread read, persisted + shipped.
    int      getClickSubdivision() const { return clickSubdivision.load (std::memory_order_acquire); }
    uint16_t getClickAccentMask()  const { return clickAccentMask.load (std::memory_order_acquire); }
    void setClickSubdivision (int subdivision);
    void setClickAccentMask (uint16_t mask);

    // Audio looper. Captures the post-chain audio (so synth and FX
    // sounds are baked into the loop) into the shared recordBuffer,
    // then plays it back as an audio-only loop. Optional click +
    // count-in run off the same metronome clock.
    bool    isLooperRecording() const { return looper.isRecording(); }
    bool    isLooperPreRolling() const { return looper.isPreRollActive(); }
    bool    isLooperPlaying() const { return looper.isPlaying(); }
    bool    isLooperLooping() const { return looper.isLooping(); }
    // Overdub mode: with a loop captured and looping on, record layers
    // the new input over the existing loop instead of replacing it.
    bool    isLooperOverdub() const { return looper.isOverdub(); }
    int     getLooperCountInBeats() const { return looper.getCountInBeats(); }
    // Fixed capture length in bars (0022): 0 = Free (stop when the user
    // stops), 1/2/4/8 = auto-stop the capture after exactly that many
    // bars so the loop always lands on the grid.
    int     getLooperLengthBars() const { return looper.getLengthBars(); }
    // Header-level click-during-capture gate, shared by the take recorder
    // and the looper: when false the click only plays during count-ins,
    // never through the take or the loop capture itself. The count-in
    // click still plays (pre-roll is independent of this toggle).
    bool    getClickDuringCapture() const { return clickDuringCapture.load (std::memory_order_acquire); }
    // Beats trimmed off the start/end of the captured loop (message-thread
    // crop settings, applied to audioLoopStart/audioLoopLength). Beat
    // granularity — finer than the bar-aligned capture trim.
    int     getLooperCropStartBeats() const { return looper.getCropStartBeats(); }
    int     getLooperCropEndBeats() const { return looper.getCropEndBeats(); }
    bool    hasAudioLoop() const { return looper.hasLoop(); }
    double  getAudioLoopPosition() const { return looper.getPosition(); }
    // Session-only playback mode (ticket 0036): reverse direction and
    // tape-style half-speed. Both default off (forward 1x) each launch and
    // are ignored while an overdub captures.
    bool    isLoopPlaybackReverse() const { return looper.isPlaybackReverse(); }
    bool    isLoopPlaybackHalfSpeed() const { return looper.isPlaybackHalfSpeed(); }
    int64_t getAudioLoopLength() const { return looper.getLength(); }
    int64_t getAudioLoopStart() const { return looper.getStart(); }
    // Total record-buffer capacity in samples (bounds a fresh capture;
    // the frontend uses it to draw capture progress).
    int     getMaxRecordSamples() const { return maxRecordSamples; }
    // Waveform peaks for the cropped loop region, recomputed on the
    // message thread whenever the loop changes (stop/trim/crop).
    const std::vector<float>& getLooperPeaks() const { return looper.getPeaks(); }
    void    setLooperRecording (bool enabled);
    void    setLooperPlaying (bool enabled);
    void    setLooperLooping (bool enabled);
    void    setLooperOverdub (bool enabled);
    void    setLoopPlaybackReverse (bool enabled);
    void    setLoopPlaybackHalfSpeed (bool enabled);
    void    setLooperCountInBeats (int beats);
    void    setLooperLengthBars (int bars);
    void    setClickDuringCapture (bool enabled);
    // Trim start/end of the loop in whole beats (4 per bar at the current
    // BPM), clamped so the window never fully collapses.
    void    setLoopCrop (int startBeats, int endBeats);
    void    clearLoop();
    // Loop layer undo/redo (0030). Undo restores the loop audio to before the
    // last overdub layer; redo re-applies it. Disabled while playing or
    // capturing. Session-only.
    bool    isLoopUndoAvailable() const { return looper.isUndoAvailable(); }
    bool    isLoopRedoAvailable() const { return looper.isRedoAvailable(); }
    void    undoLoopLayer();
    void    redoLoopLayer();
    // Converts the captured (cropped) loop into a library snippet and
    // writes WAV + JSON to the library folder when one is set — mirrors
    // the take recorder's save (one click, no dialog). Message thread
    // only. Returns the new snippet id, or -1 if there is no captured
    // loop.
    int saveLoopSnippet();

    // Load a library snippet into the looper (0056): resample it to the
    // session rate, map it onto recordBuffer's channels, publish it as the
    // current loop (clearing the layer history) and remember the source so a
    // later save is named "<source> overdub". Non-destructive: the snippet
    // and its files are never modified. Refused while a capture is active;
    // loop/snippet/take-review/melody playback is stopped first. Message
    // thread only. Returns false and sets outError on failure.
    bool    loadSnippetIntoLooper (int id, juce::String& outError);
    // Name of the snippet the current loop was loaded from ("" for a loop
    // captured in the looper). Message thread only; shipped in the transport
    // snapshot so the UI can show a "loaded from" chip.
    juce::String getLooperSourceName() const { return looperSourceName; }

    // Per-chain stem capture (0038). Session-only: when enabled, a fresh
    // take or loop capture also records the dry pass-through and each
    // record-on-capture chain's post-volume output into aligned,
    // preallocated buffers. exportStems writes one file per stem next to
    // `target` (`<base>-<stem><ext>`), reusing the snippet export formats.
    // Stems follow the capture bus only — monitor solo/mute and the master
    // Output never change them, and only the most recent capture's stems
    // are kept. Message thread only.
    void    setCaptureStemsEnabled (bool enabled);
    bool    isCaptureStemsEnabled() const { return captureStemsEnabled.load (std::memory_order_acquire); }
    bool    hasCaptureStems() const { return stemCapture.hasStems(); }
    const juce::String& getStemSource() const { return stemCapture.getSource(); }
    bool    exportStems (const juce::String& source, const juce::File& target,
                         juce::String& outError);

    // MIDI clock output for syncing external hardware (analog drum
    // machines, sequencers). One header-level toggle: when on, the clock
    // free-runs (Start + 24 ppqn) and takes / loop captures ride it,
    // re-syncing (Start again) at actual-recording / capture time.
    bool    isMidiClockEnabled()    const { return midiClockEnabled.load (std::memory_order_acquire); }
    void    setMidiClockEnabled (bool enabled);
    // "Clock: on record" — when on (and the clock toggle is on), the
    // clock does NOT free-run: it starts when a take or loop capture
    // begins (count-in pre-roll included) and stops when it ends.
    bool    isMidiClockOnRecord()   const { return midiClockOnRecord.load (std::memory_order_acquire); }
    void    setMidiClockOnRecord (bool enabled);
    juce::String getMidiOutputDeviceName() const;
    void    setMidiOutputDeviceName (const juce::String& name);
    juce::StringArray getAvailableMidiOutputDevices() const;

    juce::String getLastSaveError() const;

    // User-editable tag names for the snippet colours, persisted in the
    // properties file (`tagNames`, a JSON object keyed by colour key).
    // An empty/missing name falls back to the built-in label ("Red", …)
    // on the frontend. Message thread only.
    std::map<juce::String, juce::String> getTagNames() const;
    void setTagName (const juce::String& colorKey, const juce::String& name);

    // Named setlists (0035): ordered lists of snippet ids, persisted as a
    // JSON array under the `setlists` properties key. Message thread only.
    // Every mutation persists (debounced) and notifies libraryChanged.
    std::vector<SetlistEntry> getSetlists() const;
    juce::String createSetlist (const juce::String& name);
    bool renameSetlist (const juce::String& id, const juce::String& name);
    bool deleteSetlist (const juce::String& id);
    bool addSnippetToSetlist (const juce::String& id, int snippetId);
    bool removeSnippetFromSetlist (const juce::String& id, int snippetId);
    bool setSetlistOrder (const juce::String& id, const std::vector<int>& snippetIds);

    // Editor window size, persisted in the properties file so a resized
    // window is restored on the next launch (the JUCE standalone wrapper
    // only remembers the window position, not its size). The editor keeps
    // the aspect ratio locked, so the width is authoritative. Returns 0
    // when nothing has been saved. Message thread only.
    int  getSavedEditorWidth();
    int  getSavedEditorHeight();
    void saveEditorSize (int width, int height);

    // Crash diagnostics (Windows only): the chain-restore step currently
    // running, recorded so the unhandled-exception filter can attribute a
    // crash (e.g. one inside a hosted VST3 DLL) to the exact step. The op
    // is copied into a fixed internal buffer immediately, so any char*
    // passed in may be a temporary.
    static void setCrashOp (const char* op, const char* detail = "");
    static const char* getCrashOp();

    // True once the host called prepareToPlay with the REAL device
    // configuration (standalone device open / DAW transport prepare).
    // Plugin loads (restore at construction, manual adds) happen before
    // that — the chain's currentSampleRate/currentBlockSize still hold
    // the 44100/512 defaults, and calling plugin->prepareToPlay with
    // them would prepare every plugin twice (defaults, then the real
    // config): a full DSP teardown + rebuild with a block-size change
    // that some plugins — Neural DSP "X" heap faults — crash on. Until
    // the device is prepared, loads skip the eager prepare; the host's
    // prepareToPlay covers them (and any plugin that loads AFTER the
    // device started gets the eager prepare with the real values).
    static bool isDevicePrepared();
    static std::atomic<bool> devicePrepared;

    // Set by setStateInformation when the saved VST3 chain couldn't be
    // fully restored (e.g. a plugin's license expired). Read by the UI
    // so the user knows what was skipped. Cleared explicitly.
    juce::String getLastChainRestoreError() const;
    void clearLastChainRestoreError();
    void restoreSavedPluginChains();

    // Diagnostics (0016): a shareable, audio-free support report built on
    // the message thread — app/OS/build, crash-info.txt (current +
    // rotated), the last chain-restore error, the plugin quarantine list
    // and a settings summary (counts only, no user audio or personal
    // paths). getDiagnosticsFolder() is %APPDATA%/Retrokielto, where
    // crash-info.txt and the properties file live.
    juce::String buildDiagnosticsReport();
    juce::File getDiagnosticsFolder() const;

    // Plugin quarantine. A VST3 whose instantiation/prepare crashed the
    // app during a previous deferred restore is quarantined (persisted
    // file-name list): the restore skips it entirely — the plugin is
    // never instantiated — until the user re-adds it from the chain UI
    // (a manual add clears the entry first, giving the plugin a fresh
    // chance). The chainRestoreCrashed marker alone can't prevent this
    // crash class: it only skips state blobs, but the Neural DSP "X"
    // heap faults happen in createPluginInstance/prepareToPlay, before
    // any state is applied.
    bool isPluginQuarantined (const juce::String& fileName);
    void clearPluginQuarantineForFile (const juce::String& fileName);
    // The quarantined file names as a JSON array, for the Plugin manager UI
    // (0040). Message-thread only; loads the persisted list lazily.
    juce::var getPluginQuarantineSnapshot();

    // Record the plugin file currently being loaded in the properties
    // file ("lastPluginLoadOp", timestamped) so a crash during the load
    // — even a fail-fast that never reaches the exception filter —
    // leaves a nameable suspect for the next launch's quarantine.
    // Message thread only. Call before the load starts and after it
    // completes (notifyPluginLoadStarting/Finished).
    void notifyPluginLoadStarting (const juce::String& fileName);
    void notifyPluginLoadFinished();

    // Arm the debounced plugin-chain bundle save (all chains + the
    // blocklist + the cached scan result). Also wired into every
    // chain's onChanged. Public so the editor can persist non-chain
    // mutations that the bundle carries (e.g. a completed VST3 scan
    // updating availablePlugins, or a blocklist edit).
    void persistPluginChain();

    // True while the deferred chain restore still has work queued
    // (restoreActive, a load in flight, a queued state-blob apply, or
    // pending slots on any chain). Message-thread only.
    bool isChainRestoreInProgress() const;

    // Meter values updated by the audio thread (peak + RMS over the last block).
    float getCurrentInputLevel() const { return inputMeter.getLevel(); }
    float getCurrentInputPeak() const  { return inputMeter.getPeak(); }
    // Record meter: the actual print (dry + recordOnCapture chains), read
    // from recordingMixBuffer — unaffected by the master Output.
    float getCurrentRecordLevel() const { return recordMeter.getLevel(); }
    float getCurrentRecordPeak() const  { return recordMeter.getPeak(); }
    // Output meter: the post-master-Output monitor signal.
    float getCurrentOutputLevel() const { return outputMeter.getLevel(); }
    float getCurrentOutputPeak() const  { return outputMeter.getPeak(); }
    // Loop playback meter (post loop-level gain).
    float getCurrentLoopPlayLevel() const { return loopPlayMeter.getLevel(); }
    float getCurrentLoopPlayPeak() const  { return loopPlayMeter.getPeak(); }
    // Latched clip indicators (set by the audio thread, cleared by the UI).
    bool isInputClipped() const     { return inputMeter.isClipped(); }
    bool isRecordClipped() const    { return recordMeter.isClipped(); }
    bool isOutputClipped() const    { return outputMeter.isClipped(); }
    bool isLoopPlayClipped() const  { return loopPlayMeter.isClipped(); }
    // Clear a latched clip flag. target is "input" | "record" | "output" |
    // "loop" | "all" (unknown targets clear all). Message thread only.
    void resetClip (const juce::String& target);

    // Snippet currently being captured (only valid while recordingRequested is true).
    int getRecordingLengthSamples() const { return static_cast<int> (takeRecorder.getWritePos()); }

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

private:
    void timerCallback();
    void finalizeRecordingOnMessageThread();
    void beginActualRecording();
    void beginActualTakeOverdub();

    void renderPlayback (juce::AudioBuffer<float>& destination, int numSamples);
    void computeLevels  (const juce::AudioBuffer<float>& source, int numSamples);
    // Recomputes whether any source wants the MIDI clock running and
    // sends Start/Stop on the edges. Message thread only (may open/close
    // the output device). refreshClockRunning is the audio-thread-safe
    // version: it only updates the running flag and commands the
    // transport when an operation it owns ends on the audio thread
    // (one-shot loop finished, max-length recording filled).
    // The run condition (wantsClockRun, atomics-only so both threads can
    // evaluate it): the header-level toggle must be on, and either the
    // clock is in free-run mode (midiClockOnRecord off) or a take / loop
    // capture is active (recording or its count-in pre-roll).
    bool wantsClockRun() const;
    void updateClockRunState();
    void refreshClockRunning();

    juce::ListenerList<Listener> listeners;

    SnippetLibrary library;
    Vst3Library    vst3Library;
    // Colour key -> user tag name (message-thread only, persisted in the
    // properties file via setTagName). Keys with no user name are absent.
    std::map<juce::String, juce::String> tagNames;
    // Debounced tagNames persist (mirrors the chain persist): the actual
    // properties-file write is deferred to the 30 Hz timerCallback so it
    // never runs synchronously inside the WebView2 event dispatch.
    bool tagPersistPending = false;
    int64_t tagPersistDeadline = 0;
    void flushTagNamePersist();
    // Named setlists (0035), same debounced-persist pattern as tagNames.
    SetlistStore setlists;
    bool setlistPersistPending = false;
    int64_t setlistPersistDeadline = 0;
    void armSetlistPersist();
    void flushSetlistPersist();
    void pruneSetlistsAgainstLibrary();
    // Debounced per-snippet gain persist: the Gain knob emits on every
    // pointer move, so both the sidecar write and the (expensive) library
    // snapshot push are deferred to timerCallback. The in-memory value is
    // correct immediately; SnippetCard tracks the displayed value locally.
    bool snippetGainPersistPending = false;
    int  snippetGainPersistId = 0;
    int64_t snippetGainPersistDeadline = 0;
    void flushSnippetGainPersist();
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

    // Bundle serialise/apply + chain construction (0027 step 5b). Created in
    // the constructor with references to the members above; see
    // ChainStatePersistence.h. The restore guard, crash marker and deferred
    // driver stay in the processor.
    std::unique_ptr<ChainStatePersistence> chainStatePersistence;

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

    // Take recorder: bounded take stack + review/overdub state machine (0027,
    // 0037). The shared capture buffer (recordBuffer) and its lock stay here;
    // the module owns each take's audio and the review state. See
    // TakeRecorder.h.
    TakeRecorder takeRecorder;

    // Melody audition + per-take analysis cache (0054). The player renders
    // monitor-only in processBlock; the cache is message-thread only.
    MelodyPlayer melodyPlayer;
    std::atomic<bool> melodyAnalysing     { false };
    std::atomic<int>  melodyAnalysingTakeId { -1 };
    std::map<int, MelodyAnalyzer::Result> takeMelodies;
    void refreshMelodyPlayer();
    void stopMelodyPlayback();

    // Which melody (if any) the shared player currently holds, so the take
    // review and the library cards can show the right play/stop state.
    enum class MelodySource { none, take, snippet };
    MelodySource melodySource = MelodySource::none;
    int          melodySourceId = -1;

    // Library-snippet melody analysis: per-snippet generation counter + the
    // in-flight set (message thread only), so a newer analysis supersedes an
    // older worker and the card can show a spinner.
    int                snippetMelodyGeneration = 0;
    std::map<int, int> snippetMelodyGenerations;
    std::set<int>      snippetMelodyAnalysing;

    std::atomic<bool> playbackActive { false };
    std::atomic<int> playingSnippetId { -1 };
    std::atomic<int64_t> playbackReadPos { 0 };
    // Pending seek target in samples (-1 = none), applied by the audio
    // thread at the top of renderPlayback so a seek survives the snippet
    // re-fetch that resets playbackReadPos. Message-thread writes only.
    std::atomic<int64_t> playbackSeekSamples { -1 };

    // Metronome / count-in. Settings are user-tweakable and persisted; the
    // preRoll* / transportPosition fields are audio-thread runtime state.
    // metronomeEnabled is the header-level master click on/off, shared by
    // the take recorder, the looper and the free-running clock.
    // clickDuringCapture is the header-level gate for the click through
    // takes and loop captures (off = count-in only).
    std::atomic<bool>    metronomeEnabled { true };
    std::atomic<bool>    clickDuringCapture { true };
    std::atomic<float>   bpm              { 120.0f };
    // Tap-tempo averaging state (0049). Message-thread only.
    TapTempo             tapTempo;
    std::atomic<int>     countInBeats     { 4 };
    // Notated meter (numerator/denominator), default 4/4. Set on the message
    // thread, read on the audio thread.
    std::atomic<int>     timeSignatureNumerator   { 4 };
    std::atomic<int>     timeSignatureDenominator { 4 };

    LooperGrid::TimeSignature currentTimeSignature() const
    {
        return { timeSignatureNumerator.load (std::memory_order_acquire),
                 timeSignatureDenominator.load (std::memory_order_acquire) };
    }

    // Built-in tuner (0034). The audio thread is the single producer into
    // tunerRing (a plain float ring, preallocated in prepareToPlay); the
    // worker reads the latest tunerWindowSize samples and publishes the
    // reading. No locks on the audio thread.
    static constexpr int tunerWindowSize = 4096;
    static constexpr int tunerRingSize   = 8192;
    std::atomic<bool>    tunerOpen           { false };
    std::atomic<bool>    tunerMonitorMute    { false };
    std::atomic<float>   tunerFrequency      { 0.0f };
    std::atomic<float>   tunerConfidence     { 0.0f };
    std::atomic<float>   tunerReferencePitch { 440.0f };
    std::atomic<double>  tunerSampleRate     { 44100.0 };
    std::vector<float>   tunerRing;
    std::atomic<int64_t> tunerRingWrite      { 0 };
    class TunerWorker;
    std::unique_ptr<TunerWorker> tunerWorker;
    void startTunerWorker();
    void stopTunerWorker();

    // Audio looper state + loop-layer undo/redo (0027 step 6c). See
    // Looper.h; the shared capture buffer and record lock stay here.
    Looper looper;
    // The library snippet the current loop was loaded from (0056), or empty
    // for a captured loop. Message thread only.
    juce::String looperSourceName;
    int          looperSourceId = -1;

    // Per-chain stem capture (0038). The message thread arms/finalises
    // (allocating); the audio thread only copies into the preallocated
    // buffers. See StemCapture.h.
    StemCapture stemCapture;
    std::atomic<bool> captureStemsEnabled { false };
    void armStemsForCapture (const juce::String& source);
    void finaliseStems (int64_t length);

    void updateLoopClipLatch();
    bool canEditLoopHistory() const;
    std::atomic<float>   loopLevel        { 0.0f };
    std::atomic<float>   dryLevel         { 0.0f };
    std::atomic<float>   overdubLevel     { 0.0f };
    std::atomic<int64_t> transportPosition { 0 };
    std::atomic<int64_t> metronomePosition { 0 };
    int loopCrossfadeSamples = 0;

    void refreshLooperPeaks();
    void trimLooperToMusicalGrid();
    // Mixes a layer region [layerBase, layerBase + layerLength) into the
    // loop [loopStart, loopStart + loopLength), wrapping across loop cycles
    // pedal-style. Message thread only, run with capture stopped and
    // playback off. Shared by the looper (loopStart = audioLoopStart) and
    // the take recorder (loopStart = 0, loopLength = takeLength). clipMeter
    // latches when the mixed result reaches 0 dBFS.
    void mixOverdubLayer (int64_t loopStart, int64_t loopLength, int64_t layerBase,
                          int64_t layerLength, Meter& clipMeter);

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
    // The softer subdivision click (0050): tick pitch, fixed lower amplitude.
    std::shared_ptr<const std::vector<float>> subClickBuffer;

    // Audio-thread metronome rendering: schedules beats and rings each
    // click out across the blocks it spans. The player owns only the
    // ringing state; the waveforms above are passed in per block.
    MetronomePlayer metronomePlayer;

    // Click sound parameters (message-thread only). Persisted in host
    // state like the other metronome settings. Tuned via the "Click
    // sound" popup in the transport.
    float clickPitch        = 1000.0f;  // normal tick frequency (Hz)
    float clickAccentPitch  = 1500.0f;  // bar-first-beat frequency (Hz)
    float clickDecay        = 90.0f;    // exponential decay rate (/s)
    float clickVolume       = 0.35f;    // normal tick level
    float clickAccentVolume = 0.50f;    // accent tick level
    float clickNoise        = 0.10f;    // onset transient level

    // Click rhythm (0050). Read on the audio thread; set on the message
    // thread. 0 = beats only; the mask defaults to beat 1 of the bar.
    std::atomic<int>      clickSubdivision { 0 };
    std::atomic<uint16_t> clickAccentMask  { 0x0001 };
    void resynthesizeClicks();
    double currentSampleRate = 44100.0;

    // MIDI clock output. Clock pulses (0xF8, 24 ppqn) are generated in
    // processBlock alongside the audible metronome. MIDI Start / Stop
    // are queued from the message thread and flushed at the start of
    // the next audio block. Two header-level toggles: midiClockEnabled
    // is the master on/off — when on, the clock either free-runs
    // (midiClockOnRecord off) or runs only while a take / loop capture
    // is active (midiClockOnRecord on); takes and loop captures ride
    // whichever mode is selected and re-sync with a Start at
    // actual-recording / capture time.
    std::atomic<bool>    midiClockEnabled       { false };
    std::atomic<bool>    midiClockOnRecord      { false };
    std::atomic<bool>    clockRunning           { false };
    std::atomic<bool>    midiStartPending       { false };
    std::atomic<bool>    midiStopPending        { false };

    // The selected device + direct Start/Stop/clock sends (the standalone
    // never forwards the host MIDI buffer to hardware).
    MidiClockOutput midiClockOutput;

    void renderMidiClockInBlock (juce::MidiBuffer& midiMessages, int64_t metronomePos, int numSamples);

    // Cached audio-thread copies. Updated under the library lock briefly,
    // then held as shared_ptrs so playback can't dangle.
    std::shared_ptr<const Snippet> playbackSnippet;

    // Meter level/peak/clip state (input, record, output monitor, loop).
    Meter inputMeter;
    Meter recordMeter;
    Meter outputMeter;
    Meter loopPlayMeter;

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

    // One-shot restore of the non-chain userState at construction: sets the
    // library folder (which also auto-loads snippets), restores the snippet
    // colour tag names, and captures the launch-time properties mtime for
    // the crash self-heal anchor. VST3 chain restoration is deliberately
    // deferred to restoreSavedPluginChains()/setStateInformation.
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
    // Set while applyChainState (or a user-state restore) is running;
    // persistPluginChain skips arming while it is set. Atomic because
    // persistPluginChain can now be called from a hosted plugin's audio
    // thread (AudioProcessorListener / AudioProcessorParameter::Listener on
    // PluginChain).
    std::atomic<bool> persistingPluginChain { false };
    bool pluginChainsRestored = false;
    // Debounced chain-persist arm. persistPluginChain is callable from
    // any thread (plugins notify parameter changes from their audio
    // thread), so both fields are atomic; flushPendingChainPersist and
    // the timerCallback check run on the message thread.
    std::atomic<bool> chainPersistPending { false };
    std::atomic<int64_t> chainPersistDeadline { 0 };
    // One async plugin load in flight from the deferred restore driver
    // (timerCallback); message-thread only.
    std::atomic<int> pendingPluginLoads { 0 };
    // Set while applyChainState is running; the driver must not start a
    // load during a restore (clearChains would destroy the chain that a
    // worker thread is about to finalize into).
    bool restoreActive = false;
    // A deferred restore with queued state-blob applies is in flight:
    // the chainRestoreCrashed marker must stay set until the driver
    // drains it (timerCallback clears the marker once this flag is set
    // and the restore is no longer in progress). Clearing it inside
    // applyChainState — before the deferred slots even load — let a
    // crash in one of those loads (e.g. Archetype "X" dying in
    // setStateInformation) recur on every launch. Message-thread only.
    bool restoreRequestedThisSession = false;
    // Crash self-heal bookkeeping (0027 step 5a): the once-per-launch blob
    // decision, the crash-suspect parsing, the persisted plugin quarantine
    // and the load-op breadcrumb. See RestoreSelfHeal.h.
    RestoreSelfHeal restoreSelfHeal;

    // Properties-file modification time captured before this session
    // writes anything (restoreUserState entry). A crash-info.txt newer
    // than this was written by the launch that just crashed; anything
    // older is stale diagnostics and must not drive quarantine.
    juce::Time userStateMtimeAtLaunch;
    // True while this launch is in self-heal mode: state blobs skipped
    // (marker was set) or a crashed plugin quarantined. The in-memory
    // plugins are defaults at that point, so getStateInformation omits
    // pluginChains until the user actually mutates a chain
    // (persistPluginChain clears this) — otherwise the standalone's
    // exit capture would write the defaults into BluePrinter.settings
    // and the next launch would restore them (the "plugin state lost on
    // close" wipe). Atomic: cleared from persistPluginChain, which can
    // run on a hosted plugin's audio thread.
    std::atomic<bool> stateRestoreSkippedThisLaunch { false };
    // Saved state blob of the most recently loaded deferred slot. The
    // load callback only queues it here; the NEXT message-loop turn
    // (timerCallback, before the next slot pops) applies it. The idle
    // gap lets the plugin's queued window messages be dispatched by
    // the normal pump first — dispatching them reentrantly from inside
    // setStateInformation killed some plugins (Neural DSP "X" amp sims
    // — heap fault in the first instance's window proc). Message-thread
    // only.
    struct PendingStateApply
    {
        juce::String chainId;
        int slotIndex = -1;
        juce::MemoryBlock state;
    };
    std::unique_ptr<PendingStateApply> pendingStateApply;

    // Applies a saved chain bundle: the library config, chain construction and
    // id de-duplication live in ChainStatePersistence; this method wraps them
    // with the restore guard, the self-heal plan and the crash-marker
    // lifecycle. Suppresses chain persistence for its whole duration so a
    // restore can never echo a partial state back into the properties file.
    void applyChainState (const juce::var& state, juce::String& outError);

    // Stashed when setStateInformation fails to restore one or more
    // chain plugins (e.g. expired-license VST3s). Read by the UI on
    // open so the user knows what was skipped.
    juce::String lastChainRestoreError;

    // Fold a skipped (quarantined) plugin into the restore error the UI shows.
    void recordQuarantinedSkip (const juce::String& fileName, const juce::String& pluginName);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BluePrinterAudioProcessor)
};
