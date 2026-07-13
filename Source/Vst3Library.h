#pragma once

#include <JuceHeader.h>
#include <set>

// Folder-wide metadata for VST3 plugins: the blocklist of plugins to skip
// (e.g. ones that pop a license dialog or hang on initialize) and the
// cached scan result so the "Available plugins" list survives a restart
// without re-walking the VST3 folder.
//
// Both the MIDI chain and the audio chain reference the same library, so
// blocking a plugin or scanning the folder updates both chains at once.
// A plugin's identity is its .vst3 file path, which is unique per file
// regardless of which chain owns the slot, so a single blocklist entry
// covers both chains.
class Vst3Library
{
public:
    Vst3Library();
    ~Vst3Library() = default;

    // === Blocklist ===
    // A set of .vst3 file paths that should be skipped during scan,
    // load, and state restore. Use this for plugins that are known to
    // misbehave (e.g. expired-license plugins that pop up a modal
    // dialog or hang the host on initialize()).
    bool isBlocked (const juce::File& file) const;
    void addToBlocklist (const juce::File& file);
    bool removeFromBlocklist (const juce::File& file);
    juce::StringArray getBlocklist() const;
    void setBlocklist (const juce::StringArray& paths);
    void clearBlocklist();

    // === Cached scan result ===
    // The list of plugins the user has scanned and is allowed to add to
    // a chain. Set by the editor when a scan completes; read by the
    // editor when building the chain snapshot for the UI. Persisted in
    // host state.
    void setAvailablePlugins (const juce::var& plugins);
    juce::var getAvailablePlugins() const;

    // === Folder scanning ===
    // Walk a directory for .vst3 files and return a description for
    // every plugin type found. Blocklisted plugins are filtered out of
    // the result.
    //   { folder: "...", plugins: [...], blocklist: [...] }
    juce::var scanFolder (const juce::File& folder, juce::String& outError);

    // Describe a single .vst3 file. Returns nullptr if the file can't be
    // inspected or contains no plugins. Used by the async scanner so it
    // can stream progress per file. Blocklisted files are skipped.
    juce::var describeVst3File (const juce::File& file, juce::String& outError);

    // Async variant of describeVst3File. Runs the VST3 factory call on a
    // worker thread so a misbehaving plugin (one that pops a modal
    // license dialog or hangs in initialize()) doesn't block the message
    // thread. The callback is invoked on the message thread when the
    // worker finishes or when timeoutMs elapses, whichever comes first.
    // On timeout the worker is abandoned (C++ thread termination isn't
    // safe); the result it eventually produces is discarded.
    struct DescribeResult
    {
        juce::var descriptions;   // empty if no plugins, error, or timeout
        juce::String error;       // set when something went wrong
        bool timedOut = false;
    };
    using DescribeCallback = std::function<void (const DescribeResult&)>;
    void describeVst3FileAsync (const juce::File& file,
                                int timeoutMs,
                                DescribeCallback callback);

    // First default VST3 location for the current OS. Used as the
    // initial "available plugins" source for the UI.
    static juce::File getDefaultVst3Folder();

private:
    std::set<juce::String> blocklist;
    mutable juce::CriticalSection blocklistLock;

    juce::var availablePlugins;
    mutable juce::CriticalSection availableLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Vst3Library)
};
