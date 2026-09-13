#pragma once

#include <JuceHeader.h>

// Crash self-heal bookkeeping for the deferred VST3 chain restore,
// extracted from BluePrinterAudioProcessor (ticket 0027 step 5a): the
// once-per-launch decision to trust saved plugin state blobs, the parsing of
// the crash diagnostics that name a suspect plugin, the persisted plugin
// quarantine list and the load-op breadcrumb that covers fail-fast crashes.
//
// It is free of the audio processor and the plugin host: it talks only to the
// properties file and the crash-info / settings files, so it is unit tested
// with a temp directory (Tests/test_RestoreSelfHeal.cpp). The processor keeps
// the global crash handler + crash-op buffer, the chain make/apply code and
// the deferred-restore driver.
class RestoreSelfHeal
{
public:
    struct Paths
    {
        juce::File diagnosticsFolder;   // %APPDATA%/Retrokielto (crash-info.txt)
        juce::File settingsFile;        // %APPDATA%/BluePrinter/BluePrinter.settings
    };

    static Paths defaultPaths();

    RestoreSelfHeal();
    explicit RestoreSelfHeal (Paths pathsToUse);

    //==========================================================================
    // Per-launch self-heal plan. The properties file is written here (the
    // chainRestoreCrashed marker + timestamp) and the crash diagnostics are
    // read on EVERY call, matching the original applyChainState. The blob
    // decision is cached for the life of this object because the standalone
    // runs applyChainState twice in one launch (settings-file restore, then
    // the editor's restoreSavedPluginChains) and the second call must not
    // re-evaluate the marker the first call just wrote.
    struct Plan
    {
        bool blobsAllowed = true;     // trust + apply the saved state blobs
        bool selfHeal = false;        // !blobsAllowed or a crashed plugin named
        juce::String crashDetail;     // plugin named by crash-info.txt
        juce::String loadOpDetail;    // plugin named by lastPluginLoadOp
    };

    // `propsLaunchMtime` is the properties mtime captured before this session
    // wrote anything (fallback freshness anchor for DAW hosts, which have no
    // settings file). Pass an empty juce::Time when unavailable.
    Plan plan (juce::PropertiesFile& props, const juce::Time& propsLaunchMtime);

    // The suspects from the most recent plan() — read by the restore driver to
    // quarantine the plugin it is about to load.
    const juce::String& getCrashDetail()  const { return lastCrashDetail; }
    const juce::String& getLoadOpDetail() const { return lastLoadOpDetail; }

    // Arm / clear the crash marker (written at every restore or chain-preset
    // apply start; cleared once the deferred restore drains).
    static void setCrashMarker (juce::PropertiesFile& props);
    static void clearCrashMarker (juce::PropertiesFile& props);

    //==========================================================================
    // Plugin quarantine (persisted in the properties file, lazily loaded).
    bool     isQuarantined (juce::PropertiesFile& props, const juce::String& fileName);
    void     addQuarantined (juce::PropertiesFile& props, const juce::String& fileName);
    void     clearQuarantineForFile (juce::PropertiesFile& props, const juce::String& fileName);
    juce::var quarantineSnapshot (juce::PropertiesFile& props);

    //==========================================================================
    // Load-op breadcrumb: "<epoch millis>:<file name>" written before a plugin
    // load, cleared (empty name) when it finishes. Covers fail-fast crashes
    // that never reach the unhandled-exception filter.
    static void notifyLoadStarting (juce::PropertiesFile& props, const juce::String& fileName);
    static void notifyLoadFinished (juce::PropertiesFile& props);

    //==========================================================================
    // UI error for skipped (quarantined) plugins: accumulates the names skipped
    // in this restore and returns the composed message. resetSkipped() is
    // called at the start of a restore.
    juce::String recordSkipped (const juce::String& fileName, const juce::String& pluginName);
    void         resetSkipped() { skippedThisRestore.clear(); }

    //==========================================================================
    // Pure helpers (unit tested).
    static juce::String parseCrashOpDetail (const juce::String& crashInfoText);
    static juce::String parseLoadOpDetail (const juce::String& rawValue, const juce::Time& anchor);
    static bool blobsAllowed (bool markerWasSet, const juce::Time& markerSetAt,
                              const juce::File& settingsFile);

private:
    juce::String readFreshCrashOpDetail (const juce::Time& anchor) const;
    void ensureQuarantineLoaded (juce::PropertiesFile& props);
    void saveQuarantine (juce::PropertiesFile& props);

    Paths paths;

    bool decisionMade = false;
    bool cachedBlobsAllowed = true;

    juce::String lastCrashDetail;
    juce::String lastLoadOpDetail;

    bool quarantineLoaded = false;
    juce::StringArray quarantine;

    juce::StringArray skippedThisRestore;
};
