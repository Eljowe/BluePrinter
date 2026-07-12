#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class BluePrinterWebViewEditor : public juce::AudioProcessorEditor
                              , private juce::AudioProcessorValueTreeState::Listener
                              , private juce::AsyncUpdater
                              , private juce::Timer
{
public:
    explicit BluePrinterWebViewEditor(BluePrinterAudioProcessor&);
    ~BluePrinterWebViewEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr const char* paramGain = "Gain";

    static constexpr const char* frontendSetParameterEvent = "frontendSetParameter";
    static constexpr const char* backendParametersEvent = "backendParameters";

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    juce::String resolveWebUiUrl() const;
    juce::File getWebUiDistRoot() const;
    void emitParameterSnapshotToFrontend();
    juce::var makeParameterSnapshot() const;

    BluePrinterAudioProcessor& audioProcessor;
    juce::WebBrowserComponent webView;
    juce::Label fallbackLabel;

    std::atomic<bool> parameterUpdatePending { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BluePrinterWebViewEditor)
};
