#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SimpleEQAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SimpleEQAudioProcessorEditor (SimpleEQAudioProcessor&);
    ~SimpleEQAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label fallbackLabel;
    SimpleEQAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleEQAudioProcessorEditor)
};
