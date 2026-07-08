#include "PluginProcessor.h"
#include "PluginEditor.h"

SimpleEQAudioProcessorEditor::SimpleEQAudioProcessorEditor (SimpleEQAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    fallbackLabel.setJustificationType(juce::Justification::centred);
    fallbackLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    fallbackLabel.setText("WebView2 is not available. Install the Microsoft Edge WebView2 Runtime to use the editor.",
                          juce::dontSendNotification);
    addAndMakeVisible(fallbackLabel);

    setSize(960, 640);
}

void SimpleEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void SimpleEQAudioProcessorEditor::resized()
{
    fallbackLabel.setBounds(getLocalBounds().reduced(20));
}
