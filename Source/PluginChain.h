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

// Owns a list of VST3 plugin instances and processes audio through them
// in order. All chain mutations (add/remove/bypass/state) happen on the
// message thread; processBlock is called from the audio thread.
//
// The chain does not own its blocklist or its cached scan result — both
// live in the Vst3Library that is shared with the other chain. The
// library reference is required: passing nullptr is a programming error.
class PluginChain
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
    // block once the host reconfigures.
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    // Add a .vst3 file to the end of the chain. Returns the new slot
    // index, or -1 on failure (outError is set). Skips plugins that are
    // in the library's blocklist.
    int addPlugin (const juce::File& vst3File, juce::String& outError);

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

    // Fired after any structural change (add/remove/bypass/clear). The
    // owner uses it to push a fresh snapshot to the UI.
    std::function<void()> onChanged;

    // Serialise this chain's slots: paths, bypass flags, and each
    // plugin's internal state (base64). Used by getStateInformation.
    // Blocklist and cached scan result are NOT included — they live on
    // the shared Vst3Library.
    juce::var getChainState() const;

    // Restore this chain's slots from a previously-saved state. Skips
    // slots whose .vst3 file can't be loaded; partial restore is
    // allowed. Failed paths are reported via outError and also stashed
    // internally so the UI can display them via getLastRestoreError().
    void setChainState (const juce::var& state, juce::String& outError);

    // Error from the last setChainState call, if any plugins were
    // skipped. Cleared on each call. Returns an empty string when
    // everything loaded fine.
    juce::String getLastRestoreError() const;
    void clearLastRestoreError();

private:
    Vst3Library& library;
    std::vector<std::unique_ptr<ChainSlot>> slots;
    juce::AudioPluginFormatManager formatManager;
    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

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
