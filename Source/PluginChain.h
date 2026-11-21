#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include "Vst3Library.h"
#include "MidiChannelFilter.h"

// One slot in the FX chain. Owns the loaded plugin instance and its
// bypass flag. Plugins are serial — the output of slot N feeds the input
// of slot N+1.
//
// Slots are held by shared_ptr (see PluginChain::slots): the audio thread's
// per-block snapshot keeps a slot alive after the message thread removed it,
// so removal never frees a plugin the audio thread is still using.
struct ChainSlot
{
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    bool bypassed = false;
    juce::String name;
    juce::String path;
};

// Which of the plugin's input channels feed a chain. Bit N = channel N
// (0..7); the plugin negotiates an input bus of up to 8 channels. Read
// on the audio thread, so it's stored as a plain int bitmask. Channels
// beyond the block's channel count (e.g. "Right" in a mono layout)
// receive silence.
enum ChainInputBits : int
{
    ChainInputNone  = 0,
    ChainInputLeft  = 1,
    ChainInputRight = 2,
    ChainInputBoth  = ChainInputLeft | ChainInputRight
};

// Owns a list of VST3 plugin instances and processes audio through them
// in order. All chain mutations (add/remove/bypass/state) happen on the
// message thread; processBlock is called from the audio thread.
//
// Lifetime: processBlock iterates a shared_ptr snapshot of the slot list
// (blockSlots) that it refreshes under a try-lock. A slot removed on the
// message thread moves into retiredSlots and is only destroyed after two
// later snapshot refills, so the audio thread can never hold a dangling slot
// (and is never the thread that destroys one).
//
// The chain does not own its blocklist or its cached scan result — both
// live in the Vst3Library that is shared with the other chain. The
// library reference is required: passing nullptr is a programming error.
class PluginChain : public juce::AudioProcessorListener,
                    private juce::AudioProcessorParameter::Listener
{
public:
    // The library must outlive the chain. The processor owns both and
    // wires them up in its constructor.
    explicit PluginChain (Vst3Library& library);
    ~PluginChain();

    // Audio lifecycle. Forwards prepare/release to every loaded plugin.
    void prepareToPlay (double sampleRate, int samplesPerBlock);
    void releaseResources();

    // Run the buffer through every non-bypassed plugin in order. The
    // chain assumes the buffer already has the right channel layout; a
    // plugin that changes channel count will be reflected on the next
    // block once the host reconfigures. When wantsMidi is off the
    // plugins get an empty MIDI buffer instead of the live one.
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    // Whether this chain's plugins receive the MIDI buffer. The MIDI
    // chain defaults to true; the audio FX chain defaults to false (MIDI
    // there is opt-in via the UI toggle). Set on the message thread,
    // read on the audio thread.
    void setWantsMidi (bool wants) { wantsMidi.store (wants); }
    bool wantsMidiPass() const { return wantsMidi.load (std::memory_order_acquire); }

    // Stable identity (e.g. "chain0", "chain1"). Assigned by the
    // processor at creation and restored from saved state, so slots
    // survive renames and reordering. Message-thread only.
    const juce::String& getChainId() const { return chainId; }
    void setChainId (const juce::String& id) { chainId = id; }

    // User-facing name shown in the chain panel. Message-thread only.
    const juce::String& getName() const { return name; }
    void setName (const juce::String& newName) { name = newName; }

    // Which input channels feed this chain (ChainInputBits mask).
    // Bit N selects input channel N (0..7). Set on the message thread,
    // read on the audio thread.
    void setInputMask (int mask) { inputMask.store (mask); }
    int getInputMask() const { return inputMask.load (std::memory_order_acquire); }
    bool acceptsInputChannel (int channel) const
    {
        return (inputMask.load (std::memory_order_acquire) & (1 << channel)) != 0;
    }

    // Whether this chain's output is included in take/loop captures.
    // Set on the message thread, read on the audio thread.
    void setRecordOnCapture (bool record) { recordOnCapture.store (record); }
    bool isRecordOnCapture() const { return recordOnCapture.load (std::memory_order_acquire); }

    // Output volume in dB (-60..+12, 0 = unity). Set on the message
    // thread, read on the audio thread. The processor converts to a
    // linear gain once per block.
    void setVolumeDb (float db) { volumeDb.store (db); }
    float getVolumeDb() const { return volumeDb.load (std::memory_order_acquire); }

    // Mute toggle. A muted chain still runs its plugins (so tails and
    // internal state stay consistent) but its output is neither mixed
    // nor recorded.
    void setMuted (bool m) { muted.store (m); }
    bool isMuted() const { return muted.load (std::memory_order_acquire); }

    // Monitor-only solo/mute. These change what is HEARD but never the
    // capture: recordingMixBuffer follows the hard Mute + Record flags
    // only. If any chain is soloed, the monitor mix is only the soloed
    // chains (and the direct dry pass-through is muted, for true
    // isolation); monitorMuted removes just this chain from the monitor.
    void setMonitorSolo (bool s) { monitorSolo.store (s); }
    bool isMonitorSolo() const { return monitorSolo.load (std::memory_order_acquire); }
    void setMonitorMuted (bool m) { monitorMuted.store (m); }
    bool isMonitorMuted() const { return monitorMuted.load (std::memory_order_acquire); }

    // Per-chain output level meter (post-volume, 0..1). Written by the
    // audio thread via computeLevelsInto in the processor, read by the
    // message thread for the 30 Hz transport push.
    float getOutputLevel() const { return outputLevel.load (std::memory_order_acquire); }
    float getOutputPeak()  const { return outputPeak.load  (std::memory_order_acquire); }

    // Which MIDI channels (1..16) this chain listens to, as a bitmask
    // (bit n = channel n+1). System messages always pass through.
    // Set on the message thread, read on the audio thread.
    void setMidiChannelsMask (uint16_t mask) { midiChannelsMask.store (mask); }
    uint16_t getMidiChannelsMask() const { return midiChannelsMask.load (std::memory_order_acquire); }
    bool acceptsMidiChannel (int channel) const
    {
        return MidiChannelFilter::accepts (midiChannelsMask.load (std::memory_order_acquire), channel);
    }

    // Add a .vst3 file to the end of the chain. Returns the new slot
    // index, or -1 on failure (outError is set). Skips plugins that are
    // in the library's blocklist.
    int addPlugin (const juce::File& vst3File, juce::String& outError);

    // True when a slot in THIS chain already references the same .vst3
    // file. Used to reject same-chain duplicates: Neural DSP "X" plugins
    // crash (window-message heap fault) when two instances of the same
    // file are instantiated into one chain. Duplicates across different
    // chains are fine — each chain's slots are a separate processing
    // path — so the check is intentionally per-chain, not global.
    bool hasPluginFile (const juce::File& file) const;

    // Async variant of addPlugin. Runs findAllTypesForFile +
    // createPluginInstance on a worker thread so a misbehaving plugin
    // (one that pops up a modal license dialog, hangs in initialize(),
    // etc.) doesn't block the message thread. The callback is invoked
    // on the message thread when the worker finishes or when timeoutMs
    // elapses, whichever comes first. On timeout the worker is
    // abandoned (C++ thread termination isn't safe); the result it
    // eventually produces is discarded. On success the slot is
    // inserted into the chain and `slotIndex` is the new index;
    // on failure `slotIndex` is -1 and `error` is set.
    // Returns false (and calls callback synchronously) if the file
    // doesn't exist or is blocked.
    // NOTE: a true segfault inside a third-party DLL is not catchable
    // in-process; the blocklist is the only reliable protection
    // against repeating the load of a known-bad plugin.
    using LoadCallback = std::function<void (int slotIndex,
                                            const juce::String& name,
                                            const juce::String& error,
                                            bool timedOut)>;
    bool addPluginAsync (const juce::File& vst3File,
                         int timeoutMs,
                         LoadCallback callback);

    // Remove the slot at `index`. The slot is retired (not destroyed) so the
    // audio thread can finish the block that may still reference it; any
    // editor window for it is closed immediately via onSlotRemoved.
    bool removePlugin (int index);

    // Toggle bypass for the slot at `index`.
    bool setBypass (int index, bool shouldBypass);

    // Move the slot at fromIndex to toIndex. Both indices must be in
    // range. No-op if from == to. Fires onChanged.
    bool movePlugin (int fromIndex, int toIndex);

    // Drop every slot. The slots are retired and released once the audio
    // thread's snapshot has dropped them; only the message-thread-visible
    // list is emptied now.
    void clear();

    // Whether any slot currently has a non-bypassed plugin loaded.
    // Lock-free (an atomic count maintained by the message-thread
    // mutations), so the audio thread can call it inside processBlock.
    // The processor uses it to skip chains that have nothing to run — a
    // transparent chain's scratch is just a copy of the dry input, so
    // summing it back into the mix would double the dry signal.
    bool hasActivePlugins() const
    {
        return activePluginCount.load (std::memory_order_acquire) > 0;
    }

    // Upper bound on slots per chain. The audio-thread snapshot
    // (blockSlots) is reserved to this size in the constructor so filling
    // it never allocates; addPlugin/finalizeAsyncLoad refuse to exceed it.
    static constexpr int maxSlots = 64;

    // A retired slot is released once this many successful slot-snapshot
    // refills have happened after the retirement (see
    // collectRetiredSlots). Never tied to wall-clock time: releasing on a
    // timer could drop the last reference while the audio thread's snapshot
    // still holds it.
    static constexpr uint64_t retireGraceRefills = 2;

    // Release retired slots whose deadline has passed. Called from the
    // processor's 30 Hz timer for every live chain, so the plugin instances
    // are destroyed on the message thread, never in processBlock. `force`
    // ignores the generation and is used where the caller has just cleared
    // blockSlots with audio stopped (prepareToPlay / releaseResources).
    void collectRetiredSlots (bool force = false);

    // True while an async plugin load (addPluginAsync) is still in flight.
    // The loader keeps a raw pointer to this chain from the plugin UI
    // apartment and from its polling timer, so the owner must not destroy the
    // chain before those finish. A timed-out load stays counted: its
    // abandoned worker can still touch the chain, so the chain is leaked
    // rather than freed under it.
    bool hasLoadsInFlight() const { return loadsInFlight.load (std::memory_order_acquire) > 0; }

    // The chain's reported processing delay: the sum of getLatencySamples()
    // over loaded, non-bypassed slots (0047; the same definition 0046 uses).
    // A chain with no active plugins reports 0. Re-read on every snapshot,
    // so a plugin that changes its reported latency at runtime is picked up.
    // Message-thread only (takes the slot lock).
    int getActiveLatencySamples() const;

    // The chain's tail: the SUM of getTailLengthSeconds() over loaded,
    // non-bypassed slots, because a chain's slots are serial (slot N's tail
    // runs through slot N+1). The processor takes the MAX over its parallel
    // chains. A host query, never called from processBlock. Message-thread
    // only (takes the slot lock).
    double getTailLengthSeconds() const;

    int getNumPlugins() const;
    juce::AudioPluginInstance* getPlugin (int index) const;
    juce::String getSlotName (int index) const;
    juce::String getSlotPath (int index) const;
    bool isSlotBypassed (int index) const;

    // Per-slot editor window lifetime. The chain doesn't show windows
    // itself — the WebViewEditor owns the DialogWindow instances — but it
    // needs to know when a slot disappears so it can ask the editor to
    // close any open window for that slot. `onSlotRemoved` is set by the
    // owner and called with the removed slot index.
    std::function<void (int)> onSlotRemoved;

    // Fired after any structural change (add/remove/bypass/clear), and
    // also when a hosted plugin notifies its listeners of a parameter
    // or structure change (AudioProcessorListener callbacks below).
    // NOTE: the parameter-changed callback may fire from a plugin's
    // audio thread, so this callback must stay lightweight and
    // thread-safe (the owner only uses it to arm a debounced persist).
    std::function<void()> onChanged;

    // AudioProcessorListener: host-plugin parameter/structure changes.
    // Called by the plugin instance (possibly from its audio thread);
    // forwards to onChanged so parameter tweaks inside a plugin's
    // editor arm the same debounced persistence as structural changes.
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override
    {
        if (onChanged)
            onChanged();
    }

    void audioProcessorChanged (juce::AudioProcessor* plugin,
                               const juce::AudioProcessorListener::ChangeDetails& details) override
    {
        // parameterInfoChanged means the plugin rebuilt/renamed its
        // parameter list, so re-bind our direct parameter listeners to
        // include any new ones. Message-thread only: addListener/
        // removeListener must not run inside a plugin's audio-thread
        // callback.
        if (details.parameterInfoChanged
            && plugin != nullptr
            && juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            detachParameterListeners (*plugin);
            attachParameterListeners (*plugin);
        }
        if (onChanged)
            onChanged();
    }

    // AudioProcessorParameter::Listener. The AudioProcessorListener callbacks
    // above do NOT fire for ordinary parameter edits by modern plugins: JUCE's
    // AudioProcessor::ParameterChangeForwarder is registered only by
    // LegacyAudioParameter, so an APVTS-based plugin that just calls
    // setValueNotifyingHost (rather than updateHostDisplay/setDirty) never
    // arms the owner's debounced persist — its state was silently never
    // re-saved. Listening to each hosted parameter directly closes that gap.
    // May fire from the plugin's audio thread; onChanged only arms atomics
    // (see persistPluginChain), so this stays realtime-safe.
    void parameterValueChanged (int, float) override
    {
        if (onChanged)
            onChanged();
    }

    void parameterGestureChanged (int, bool) override {}

    // Serialise this chain's slots: paths, bypass flags, and each
    // plugin's internal state (base64). Used by getStateInformation.
    // Blocklist and cached scan result are NOT included — they live on
    // the shared Vst3Library.
    juce::var getChainState() const;

    // Restore this chain's slots from a previously-saved state. Skips
    // slots whose .vst3 file can't be loaded or that duplicate an
    // earlier slot in this chain; the remaining slots are DEFERRED —
    // they are queued as pending slots, not instantiated here (see
    // PendingSlot below). Failed paths are reported via outError and
    // also stashed internally so the UI can display them via
    // getLastRestoreError().
    void setChainState (const juce::var& state, juce::String& outError);

    // Apply a named-preset payload (0033): replaces the chain's slots and
    // its output volume/mute and MIDI toggle/channel filter, WITHOUT touching
    // the chain's id, name, input mask or record-on-capture flag (those are
    // project wiring, not the rig). Slots are DEFERRED exactly like
    // setChainState — queued as PendingSlots for the processor's restore
    // driver. outError lists any slots skipped (blocked / duplicate).
    void applyPresetState (const juce::var& presetPayload, juce::String& outError);

    // A plugin slot restored from saved state but not yet instantiated.
    // The processor's restore driver (timerCallback) loads pending
    // slots one per message-loop turn. Instantiating several plugins
    // synchronously in a row keeps the message thread inside plugin
    // code for hundreds of ms; window messages the plugins queue during
    // their own setup then get dispatched reentrantly and crash some
    // plugins (Neural DSP "X" amp sims — heap fault in the window proc
    // of the first instance). Loading one slot per loop turn gives
    // every plugin's pending messages an idle moment to fire safely.
    struct PendingSlot
    {
        juce::File file;
        juce::String name;
        bool bypassed = false;
        juce::String stateBase64;
    };
    bool hasPendingSlots() const;
    PendingSlot popPendingSlot();

    // Error from the last setChainState call, if any plugins were
    // skipped. Cleared on each call. Returns an empty string when
    // everything loaded fine.
    juce::String getLastRestoreError() const;
    void clearLastRestoreError();

    // Output level meter state. Deliberately public: the audio thread
    // writes these every block (via the processor's computeLevelsInto)
    // and the message thread reads them for the 30 Hz transport push.
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<float> outputPeak  { 0.0f };

    // Smoothed per-chain CPU load as a fraction of the block budget
    // (elapsed / (numSamples / sampleRate)), written by the audio thread
    // in the processor's chain loop via ChainCpuMath and read by the
    // message thread for the 30 Hz transport push. Public for the same
    // reason as the output meters.
    std::atomic<float> cpuFraction { 0.0f };
    float getCpuFraction() const { return cpuFraction.load (std::memory_order_acquire); }

private:
    Vst3Library& library;

    // Slots, message-thread mutated. shared_ptr so the audio-thread snapshot
    // (blockSlots) and retiredSlots can hold a slot after it has been
    // removed from this list.
    std::vector<std::shared_ptr<ChainSlot>> slots;

    // Audio-thread snapshot of `slots` for the current block. Refilled in
    // processBlock only when `lock` can be entered without blocking; on
    // contention the previous block's snapshot is reused. Reserved to
    // maxSlots in the constructor so refilling never allocates.
    std::vector<std::shared_ptr<ChainSlot>> blockSlots;

    // Slots removed from `slots` but not yet destroyed. Kept until the
    // audio thread's snapshot can no longer reference them (see
    // collectRetiredSlots). Message-thread only.
    struct RetiredSlot
    {
        std::shared_ptr<ChainSlot> slot;
        uint64_t deadlineGeneration = 0;
    };
    std::vector<RetiredSlot> retiredSlots;

    // Successful `blockSlots` refills, bumped only after a refill under
    // tryEnter succeeds (never when the lock is contended and the previous
    // snapshot is reused). Retirement deadlines are expressed against it.
    std::atomic<uint64_t> slotSnapshotGeneration { 0 };

    // Number of slots holding a loaded, non-bypassed plugin. Maintained on
    // the message thread so hasActivePlugins() is lock-free on the audio
    // thread.
    std::atomic<int> activePluginCount { 0 };
    void refreshActivePluginCount();

    // In-flight async loads (see hasLoadsInFlight).
    std::atomic<int> loadsInFlight { 0 };

    // True between prepareToPlay and releaseResources. The destructor only
    // calls releaseResources() on still-prepared plugins (a plugin the host
    // already released must not be released twice).
    bool prepared = false;

    std::vector<PendingSlot> pendingSlots;
    juce::AudioPluginFormatManager formatManager;
    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;
    std::atomic<bool> wantsMidi { true };

    // Stable id + display name (message-thread only), input channel
    // mask, capture-recording toggle, volume, mute, and MIDI channel
    // filter (audio-thread reads).
    juce::String chainId;
    juce::String name;
    std::atomic<int>     inputMask { ChainInputBoth };
    std::atomic<bool>    recordOnCapture { true };
    std::atomic<float>   volumeDb { 0.0f };
    std::atomic<bool>    muted { false };
    std::atomic<bool>    monitorSolo { false };
    std::atomic<bool>    monitorMuted { false };
    std::atomic<uint16_t> midiChannelsMask { 0xFFFF };

    mutable juce::CriticalSection lock;

    // Error from the most recent setChainState call, if any plugins
    // were skipped. Guarded by restoreErrorLock.
    juce::String lastRestoreError;
    mutable juce::CriticalSection restoreErrorLock;

    juce::AudioPluginInstance* createInstance (const juce::File& file,
                                               juce::String& outName,
                                               juce::String& outError);

    // Attach/detach this chain as an AudioProcessorParameter::Listener to
    // every parameter of a hosted plugin, so parameter edits arm the
    // debounced persist even when the plugin doesn't notify
    // AudioProcessorListener (see parameterValueChanged).
    void attachParameterListeners (juce::AudioProcessor& plugin);
    void detachParameterListeners (juce::AudioProcessor& plugin);

    // Queue the slots of a saved/preset state as PendingSlots, skipping
    // blocklisted and same-chain-duplicate files. Shared by setChainState
    // and applyPresetState so the deferred-load rules live in one place.
    void queueSlotsFromState (const juce::var& slotArray,
                              juce::StringArray& failedPaths);

    // Apply the mix/MIDI rig fields shared by a saved state and a preset
    // (volume, muted, wantsMidi, midiChannels). Identity/routing fields are
    // the caller's business.
    void applyRigSettings (const juce::DynamicObject& obj);

    // Called on the message thread by addPluginAsync's waiter after the
    // worker thread successfully creates an instance. Calls
    // prepareToPlay, inserts a new slot, and fires onChanged. Returns
    // the new slot index.
    int finalizeAsyncLoad (std::unique_ptr<juce::AudioPluginInstance> instance,
                           const juce::String& name,
                           const juce::File& file);
};
