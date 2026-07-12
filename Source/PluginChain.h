#pragma once

#include <JuceHeader.h>

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
class PluginChain
{
public:
    PluginChain();
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
    // index, or -1 on failure (outError is set).
    int addPlugin (const juce::File& vst3File, juce::String& outError);

    // Remove the slot at `index`. Closes any editor window for the slot
    // before destroying the plugin.
    bool removePlugin (int index);

    // Toggle bypass for the slot at `index`.
    bool setBypass (int index, bool shouldBypass);

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

    // Serialise the whole chain: paths, bypass flags, and each plugin's
    // internal state (base64). Used by getStateInformation.
    juce::var getChainState() const;

    // Restore from a previously-saved chain state. Skips slots whose
    // .vst3 file can't be loaded; partial restore is allowed.
    void setChainState (const juce::var& state, juce::String& outError);

    // Walk a directory for .vst3 files and return a description for
    // every plugin type found. Returned object:
    //   { folder: "...", plugins: [ { name, path, manufacturer }, ... ] }
    static juce::var scanFolder (const juce::File& folder, juce::String& outError);

    // Describe a single .vst3 file. Returns nullptr if the file can't be
    // inspected or contains no plugins. Used by the async scanner so it
    // can stream progress per file.
    static juce::var describeVst3File (const juce::File& file, juce::String& outError);

    // First default VST3 location for the current OS. Used as the
    // initial "available plugins" source for the UI.
    static juce::File getDefaultVst3Folder();

private:
    std::vector<std::unique_ptr<ChainSlot>> slots;
    juce::AudioPluginFormatManager formatManager;
    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    mutable juce::CriticalSection lock;

    juce::AudioPluginInstance* createInstance (const juce::File& file,
                                               juce::String& outName,
                                               juce::String& outError);
};
