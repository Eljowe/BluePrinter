/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebViewEditor.h"

//==============================================================================
BluePrinterAudioProcessor::BluePrinterAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

BluePrinterAudioProcessor::~BluePrinterAudioProcessor()
{
}

//==============================================================================
const juce::String BluePrinterAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool BluePrinterAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool BluePrinterAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool BluePrinterAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double BluePrinterAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int BluePrinterAudioProcessor::getNumPrograms()
{
    return 1;
}

int BluePrinterAudioProcessor::getCurrentProgram()
{
    return 0;
}

void BluePrinterAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String BluePrinterAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void BluePrinterAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void BluePrinterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

void BluePrinterAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool BluePrinterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
   #endif

    return true;
  #endif
}
#endif

void BluePrinterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const auto gain = apvts.getRawParameterValue("Gain")->load();

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.applyGain (channel, 0, buffer.getNumSamples(), gain);
}

//==============================================================================
juce::AudioProcessorEditor* BluePrinterAudioProcessor::createEditor()
{
    auto webViewOptions = juce::WebBrowserComponent::Options{}
                              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2);

    if (!juce::WebBrowserComponent::areOptionsSupported(webViewOptions))
        return new BluePrinterAudioProcessorEditor(*this);

    return new BluePrinterWebViewEditor(*this);
}

bool BluePrinterAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
void BluePrinterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void BluePrinterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessorValueTreeState::ParameterLayout BluePrinterAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "Gain",
        "Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.5f));

    return layout;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BluePrinterAudioProcessor();
}
