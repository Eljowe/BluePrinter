#pragma once

#include <JuceHeader.h>
#include "Vst3Library.h"

// One slot in the FX chain. Owns the loaded plugin instance and its
// bypass flag. Plugins are serial — the output of slot N feeds the input
// of slot N+1.
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
// The chain does not own its blocklist or its cached scan result — both
// live in the Vst3Library that is shared with the other chain. The
// library reference is required: passing nullptr is a programming error.
class PluginChain : public juce::AudioProcessorListener
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
        // channel 0 = system message (clock, start, stop…) — always pass.
        if (channel <= 0)
            return true;
        const uint16_t bit = static_cast<uint16_t> (1u << (channel - 1));
        return (midiChannelsMask.load (std::memory_order_acquire) & bit) != 0;
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

    // Remove the slot at `index`. Closes any editor window for the slot
    // before destroying the plugin.
    bool removePlugin (int index);

    // Toggle bypass for the slot at `index`.
    bool setBypass (int index, bool shouldBypass);

    // Move the slot at fromIndex to toIndex. Both indices must be in
    // range. No-op if from == to. Fires onChanged.
    bool movePlugin (int fromIndex, int toIndex);

    // Drop every slot.
    void clear();

    // Whether any slot currently has a non-bypassed plugin loaded.
    // Safe to call from the audio thread (short lock). The processor
    // uses it to skip chains that have nothing to run — a transparent
    // chain's scratch is just a copy of the dry input, so summing it
    // back into the mix would double the dry signal.
    bool hasActivePlugins() const
    {
        const juce::ScopedLock sl (lock);
        for (auto& slot : slots)
            if (slot->plugin != nullptr && ! slot->bypassed)
                return true;
        return false;
    }

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

    void audioProcessorChanged (juce::AudioProcessor*,
                               const juce::AudioProcessorListener::ChangeDetails&) override
    {
        if (onChanged)
            onChanged();
    }

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

private:
    Vst3Library& library;
    std::vector<std::unique_ptr<ChainSlot>> slots;
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

    // Called on the message thread by addPluginAsync's waiter after the
    // worker thread successfully creates an instance. Calls
    // prepareToPlay, inserts a new slot, and fires onChanged. Returns
    // the new slot index.
    int finalizeAsyncLoad (std::unique_ptr<juce::AudioPluginInstance> instance,
                           const juce::String& name,
                           const juce::File& file);
};
