#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

#include "PluginChain.h"
#include "Vst3Library.h"

// Serialises and applies the plugin-chain bundle, extracted from
// BluePrinterAudioProcessor (ticket 0027 step 5b). It owns the bundle format
// (chains + nextChainId + the folder-wide blocklist + cached scan result), the
// chain list construction during a restore, the id de-duplication and the
// legacy/pre-split migration application. The migration rules themselves live
// in ChainStateMigration; the deferred-restore guard, crash marker and
// skip-state-blobs policy stay in the processor.
//
// It holds references to the live chain list and library (it does not own
// them), so the processor's audio thread keeps iterating the same containers.
// Message-thread only. Verified by the full plugin build (the module depends
// on PluginChain, which the pure-logic test target excludes).
class ChainStatePersistence
{
public:
    struct Deps
    {
        std::vector<std::unique_ptr<PluginChain>>& chains;
        juce::CriticalSection& chainLock;
        Vst3Library& vst3;
        int& nextChainId;
        // Wired into every created chain's onChanged (the debounced persist).
        std::function<void()> onChainChanged;
    };

    explicit ChainStatePersistence (Deps depsToUse);

    // Build the combined bundle written to host state and the properties file.
    juce::var makeState() const;

    // Inverse of makeState: restore the folder-wide library config, clear the
    // chains, then create and load each saved chain (current / legacy-split /
    // pre-split, via ChainStateMigration::normalise), fixing the id counter and
    // duplicates. The caller owns the restore guard and the crash marker.
    void applyState (const juce::var& state, juce::String& outError);

    // Read the saved bundle from the properties file, preferring the current
    // "pluginChains" key whenever it parses to an object (even an empty
    // chains array) so a stale legacy "pluginChain" can't shadow it.
    static juce::var loadSavedState (juce::PropertiesFile& props);

    // Create a chain with the given config and push it into the list, wiring
    // the debounced persist. Does NOT persist or notify.
    PluginChain* createChain (const juce::String& name, int inputMask,
                              bool wantsMidi, bool recordOnCapture);

    // Drop every chain (restore path). Fires onSlotRemoved so the editor can
    // close any open plugin windows.
    void clearChains();

    // Reassign fresh ids to any chain whose id is missing or duplicated, and
    // bump nextChainId past the highest id in use.
    void ensureUniqueChainIds();

private:
    Deps deps;
};
