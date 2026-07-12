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
    static constexpr const char* frontendSaveSnippetEvent      = "frontendSaveSnippet";
    static constexpr const char* frontendRevealSnippetEvent    = "frontendRevealSnippet";
    static constexpr const char* frontendChooseFolderEvent     = "frontendChooseLibraryFolder";
    static constexpr const char* frontendOpenFolderEvent       = "frontendOpenLibraryFolder";

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

    std::atomic<bool> parameterUpdatePending { false };
    std::atomic<bool> libraryUpdatePending   { false };
    std::atomic<bool> transportUpdatePending { false };

    // Held by the FileChooser callbacks. Reset once the dialog closes.
    std::unique_ptr<juce::FileChooser> activeFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BluePrinterWebViewEditor)
};
