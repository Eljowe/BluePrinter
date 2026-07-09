#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ObstacleAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ObstacleAudioProcessorEditor (ObstacleAudioProcessor&);
    ~ObstacleAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label fallbackLabel;
    ObstacleAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ObstacleAudioProcessorEditor)
};
