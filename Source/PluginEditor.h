#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class BluePrinterAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit BluePrinterAudioProcessorEditor (BluePrinterAudioProcessor&);
    ~BluePrinterAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label fallbackLabel;
    BluePrinterAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BluePrinterAudioProcessorEditor)
};
