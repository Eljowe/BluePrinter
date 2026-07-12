#pragma once

#include <JuceHeader.h>
#include <thread>
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
    static constexpr const char* frontendSaveSnippetEvent      = "frontendSaveSnippet";
    static constexpr const char* frontendRevealSnippetEvent    = "frontendRevealSnippet";
    static constexpr const char* frontendChooseFolderEvent     = "frontendChooseLibraryFolder";
    static constexpr const char* frontendOpenFolderEvent       = "frontendOpenLibraryFolder";
    static constexpr const char* frontendRefreshLibraryEvent   = "frontendRefreshLibrary";
    static constexpr const char* frontendSetMetronomeEvent     = "frontendSetMetronome";
    static constexpr const char* frontendSetBpmEvent           = "frontendSetBpm";
    static constexpr const char* frontendSetCountInBeatsEvent  = "frontendSetCountInBeats";
    static constexpr const char* frontendAddVst3Event          = "frontendAddVst3";
    static constexpr const char* frontendRemoveVst3Event       = "frontendRemoveVst3";
    static constexpr const char* frontendSetVst3BypassEvent    = "frontendSetVst3Bypass";
    static constexpr const char* frontendOpenVst3EditorEvent   = "frontendOpenVst3Editor";
    static constexpr const char* frontendCloseVst3EditorEvent  = "frontendCloseVst3Editor";
    static constexpr const char* frontendScanVst3FolderEvent   = "frontendScanVst3Folder";
    static constexpr const char* frontendGetVst3ChainEvent     = "frontendGetVst3Chain";

    static constexpr const char* backendVst3ChainEvent        = "backendVst3Chain";
    static constexpr const char* backendVst3ScanProgressEvent = "backendVst3ScanProgress";

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
    void openVst3Editor (int slotIndex);
    void closeVst3Editor (int slotIndex, bool deleteAfterClose);
    void closeAllVst3Editors();
    void emitVst3ChainSnapshot();
    void scanVst3Folder (const juce::File& folder);
    void pickVst3FileAndAdd();
    void pickVst3FolderAndScan();

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
    void emitLibraryToFrontend();
    juce::var makeParameterSnapshot() const;

    void saveSnippetWithDialog(int snippetId, const juce::File& startingFolder);
    void pickLibraryFolder(const juce::File& startingFolder);
    void pickLibraryFolderThenSave(int pendingSnippetId);
    void sendNotification(const juce::String& message, const juce::String& level);

    BluePrinterAudioProcessor& audioProcessor;
    juce::WebBrowserComponent webView;
    juce::Label fallbackLabel;

    // Per-slot native VST3 editor windows. Index in the map matches the
    // slot's index in the chain. The unique_ptr owns the DialogWindow,
    // which in turn owns the AudioProcessorEditor.
    std::map<int, std::unique_ptr<juce::DialogWindow>> vst3EditorWindows;

    // Last scan result (list of available plugins), kept so the chain
    // snapshot can include it.
    juce::var lastVst3Scan { new juce::DynamicObject() };
    juce::CriticalSection vst3ScanLock;

    std::atomic<bool> parameterUpdatePending { false };
    std::atomic<bool> libraryUpdatePending   { false };
    std::atomic<bool> transportUpdatePending { false };
    std::atomic<bool> chainUpdatePending     { false };

    // Held by the FileChooser callbacks. Reset once the dialog closes.
    std::unique_ptr<juce::FileChooser> activeFileChooser;

    // Background VST3 scanner. Owned by the editor for its lifetime; the
    // thread emits progress through juce::MessageManager::callAsync so it
    // always runs back on the message thread.
    std::unique_ptr<std::thread> scanThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BluePrinterWebViewEditor)
};
