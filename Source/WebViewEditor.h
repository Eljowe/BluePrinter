#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class BluePrinterWebViewEditor : public juce::AudioProcessorEditor
                              , private juce::AudioProcessorValueTreeState::Listener
                              , private BluePrinterAudioProcessor::Listener
                              , private juce::AsyncUpdater
                              , private juce::Timer
{
public:
    explicit BluePrinterWebViewEditor(BluePrinterAudioProcessor&);
    ~BluePrinterWebViewEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Event names shared with the React frontend.
    static constexpr const char* paramGain = "Gain";

    static constexpr const char* frontendSetParameterEvent     = "frontendSetParameter";
    static constexpr const char* frontendStartRecordingEvent   = "frontendStartRecording";
    static constexpr const char* frontendStopRecordingEvent    = "frontendStopRecording";
    static constexpr const char* frontendStartPlaybackEvent    = "frontendStartPlayback";
    static constexpr const char* frontendStopPlaybackEvent     = "frontendStopPlayback";
    static constexpr const char* frontendUpdateSnippetEvent    = "frontendUpdateSnippetMeta";
    static constexpr const char* frontendDeleteSnippetEvent    = "frontendDeleteSnippet";
    static constexpr const char* frontendDetectSnippetKeyEvent = "frontendDetectSnippetKey";
    static constexpr const char* frontendSaveSnippetEvent      = "frontendSaveSnippet";
    static constexpr const char* frontendRevealSnippetEvent    = "frontendRevealSnippet";
    static constexpr const char* frontendChooseFolderEvent     = "frontendChooseLibraryFolder";
    static constexpr const char* frontendOpenFolderEvent       = "frontendOpenLibraryFolder";
    static constexpr const char* frontendRefreshLibraryEvent   = "frontendRefreshLibrary";
    // Fired by the React app once the WebView has loaded, so the
    // backend can push a fresh snippet snapshot. The processor's
    // restoreUserState loads snippets from disk before the editor
    // is constructed, so the corresponding libraryChanged listener
    // notification fires before any listener is registered and is
    // lost. The initial withInitialisationData blob can also race
    // the page being ready to read it, so we don't rely on it
    // either. Asking once on mount is the belt-and-suspenders fix
    // that mirrors what the chain UI already does.
    static constexpr const char* frontendGetSnippetsEvent      = "frontendGetSnippets";
    static constexpr const char* frontendSetMetronomeEvent     = "frontendSetMetronome";
    static constexpr const char* frontendSetBpmEvent           = "frontendSetBpm";
    static constexpr const char* frontendSetCountInBeatsEvent  = "frontendSetCountInBeats";
    static constexpr const char* frontendAddVst3Event          = "frontendAddVst3";
    static constexpr const char* frontendRemoveVst3Event       = "frontendRemoveVst3";
    static constexpr const char* frontendMoveVst3Event         = "frontendMoveVst3";
    static constexpr const char* frontendSetVst3BypassEvent    = "frontendSetVst3Bypass";
    static constexpr const char* frontendOpenVst3EditorEvent   = "frontendOpenVst3Editor";
    static constexpr const char* frontendCloseVst3EditorEvent  = "frontendCloseVst3Editor";
    static constexpr const char* frontendScanVst3FolderEvent   = "frontendScanVst3Folder";
    static constexpr const char* frontendGetVst3ChainEvent     = "frontendGetVst3Chain";
    static constexpr const char* frontendBlockVst3PluginEvent  = "frontendBlockVst3Plugin";
    static constexpr const char* frontendUnblockVst3PluginEvent = "frontendUnblockVst3Plugin";

    static constexpr const char* backendVst3ChainEvent        = "backendVst3Chain";
    static constexpr const char* backendVst3ScanProgressEvent = "backendVst3ScanProgress";
    static constexpr const char* backendVst3LoadFailedEvent   = "backendVst3LoadFailed";

    static constexpr const char* backendParametersEvent = "backendParameters";
    static constexpr const char* backendSnippetsEvent   = "backendSnippets";
    static constexpr const char* backendTransportEvent  = "backendTransport";
    static constexpr const char* backendNotifyEvent     = "backendNotify";

    // File-chooser / dialog handlers — public so the event-listener lambdas
    // registered inside makeWebViewOptions can call them.
    void handleSaveSnippet(const juce::var& data);
    void handleRevealSnippet(const juce::var& data);
    void handleChooseLibraryFolder();
    void handleOpenLibraryFolder();

    // VST3 chain handlers — public so the listener lambdas can drive them.
    // The "chain" argument is "midiChain" or "audioChain" and picks
    // which of the two chains the operation targets. Index is the
    // slot index within that chain.
    void openVst3Editor (const juce::String& chain, int slotIndex);
    void closeVst3Editor (const juce::String& chain, int slotIndex, bool deleteAfterClose);
    void closeAllVst3Editors();
    void emitVst3ChainSnapshot();
    // Push a fresh snippet + library-folder snapshot to the
    // WebView. The frontendGetSnippetsEvent listener inside
    // makeWebViewOptions calls this when React asks for a fresh
    // copy on mount, so it has to be public like the other
    // emit*Snapshot methods.
    void emitLibraryToFrontend();
    // Rebuild the vst3EditorWindows maps so the windows are keyed by
    // their plugin's current slot index. Called after a reorder so
    // windows that survived the move stay attached to the right slot
    // (the underlying plugin instance is unchanged — only the index
    // shifted). Matches by slot path.
    void rekeyVst3EditorWindows();
    void scanVst3Folder (const juce::File& folder);
    void scanDefaultVst3Folder();
    void pickVst3FileAndAdd (const juce::String& chain);
    void addVst3FromPath (const juce::String& chain, const juce::File& vst3File);

    // Snapshot helpers — public so the listener lambdas can use them.
    juce::var makeSnippetsSnapshot() const;
    juce::var makeTransportSnapshot() const;

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    // BluePrinterAudioProcessor::Listener
    void libraryChanged() override;
    void transportChanged() override;
    void pluginChainChanged() override;

    juce::String resolveWebUiUrl() const;
    juce::File getWebUiDistRoot() const;
    void emitParameterSnapshotToFrontend();
    void emitTransportToFrontend();
    juce::var makeParameterSnapshot() const;

    // Drives the next file of an in-flight VST3 scan. Called from
    // timerCallback on the message thread.
    void runScanStep();
    void emitScanProgress (bool active, int current, int total, const juce::String& currentFile);

    void saveSnippetWithDialog(int snippetId, const juce::File& startingFolder);
    void pickLibraryFolder(const juce::File& startingFolder);
    void pickLibraryFolderThenSave(int pendingSnippetId);
    void sendNotification(const juce::String& message, const juce::String& level);

    BluePrinterAudioProcessor& audioProcessor;
    juce::WebBrowserComponent webView;
    juce::Label fallbackLabel;

    // Per-slot native VST3 editor windows, one map per chain. The key
    // in each map matches the slot's index in that chain. The
    // unique_ptr owns the DialogWindow, which in turn owns the
    // AudioProcessorEditor. The actual stored type is the
    // Vst3EditorWindow subclass declared in WebViewEditor.cpp; we
    // keep the map's pointer type as DialogWindow because the
    // subclass lives in an anonymous namespace and can't be named in
    // this header.
    std::map<int, std::unique_ptr<juce::DialogWindow>> midiEditorWindows;
    std::map<int, std::unique_ptr<juce::DialogWindow>> audioEditorWindows;

    std::atomic<bool> parameterUpdatePending { false };
    std::atomic<bool> libraryUpdatePending   { false };
    std::atomic<bool> transportUpdatePending { false };
    std::atomic<bool> chainUpdatePending     { false };

    // Held by the FileChooser callbacks. Reset once the dialog closes.
    std::unique_ptr<juce::FileChooser> activeFileChooser;

    // In-flight VST3 scan. Lives on the message thread and is driven one
    // file at a time from timerCallback() so per-file progress is visible
    // without blocking the message loop. The per-file describe runs on a
    // worker thread (see Vst3Library::describeVst3FileAsync) so a
    // misbehaving .vst3 — modal license dialog, hung initialize(),
    // throwing factory call — can be timed out and skipped without
    // bringing the message thread (and the whole UI) down.
    struct Vst3ScanState
    {
        juce::File folder;
        juce::Array<juce::File> files;
        int currentIndex = 0;
        juce::Array<juce::var> pluginArray;
        // True between the kick-off of an async describe and its
        // completion callback. runScanStep uses this to avoid stacking
        // multiple per-file workers.
        bool asyncScanInFlight = false;
    };
    std::unique_ptr<Vst3ScanState> activeScan;

    // Lifetime guard for async scan callbacks. The DescribeWaiter
    // posted by describeVst3FileAsync captures a weak_ptr to aliveToken
    // and bails out if the editor has been destroyed (or the token
    // explicitly reset) before the worker finishes. Set in the
    // destructor before other members are torn down so any in-flight
    // callback that fires after the destructor returns is a no-op.
    std::shared_ptr<int> aliveToken = std::make_shared<int> (0);
    std::atomic<bool> disposed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BluePrinterWebViewEditor)
};
