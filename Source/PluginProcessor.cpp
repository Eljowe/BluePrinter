/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebViewEditor.h"

//==============================================================================
SimpleEQAudioProcessor::SimpleEQAudioProcessor()
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

SimpleEQAudioProcessor::~SimpleEQAudioProcessor()
{
}

//==============================================================================
const juce::String SimpleEQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SimpleEQAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SimpleEQAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SimpleEQAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SimpleEQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SimpleEQAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SimpleEQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SimpleEQAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SimpleEQAudioProcessor::getProgramName (int index)
{
    return {};
}

void SimpleEQAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void SimpleEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    
    juce::dsp::ProcessSpec spec;
    
    spec.maximumBlockSize = samplesPerBlock;
    
    spec.numChannels = 1;
    
    spec.sampleRate = sampleRate;
    
    leftChain.prepare(spec);
    rightChain.prepare(spec);
    
    updateFilters();
    
    leftChannelFifo.prepare(samplesPerBlock);
    rightChannelFifo.prepare(samplesPerBlock);
    
    osc.initialise([](float x) { return std::sin(x); });
    
    spec.numChannels = getTotalNumOutputChannels();
    osc.prepare(spec);
    osc.setFrequency(440);

    inputClippingDetected.store(false, std::memory_order_release);
    outputClippingDetected.store(false, std::memory_order_release);
    inputPeakLevel.store(0.0f, std::memory_order_release);
    outputPeakLevel.store(0.0f, std::memory_order_release);
    compressorGainReductionDb = { 0.0f, 0.0f };
    distortionToneState = { 0.0f, 0.0f };
}

void SimpleEQAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SimpleEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (//layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SimpleEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    auto chainSettings = getChainSettings(apvts);

    applyGain(buffer, chainSettings.inputGainInDecibels);

    bool didInputClip = false;
    float inputPeak = 0.0f;
    for( int channel = 0; channel < buffer.getNumChannels(); ++channel )
    {
        const auto* channelData = buffer.getReadPointer(channel);
        for( int sample = 0; sample < buffer.getNumSamples(); ++sample )
        {
            const auto absSample = std::abs(channelData[sample]);
            inputPeak = juce::jmax(inputPeak, absSample);
            didInputClip = didInputClip || (absSample >= 1.0f);
        }
    }

    if( didInputClip )
        inputClippingDetected.store(true, std::memory_order_release);

    pushPeakLevel(inputPeakLevel, inputPeak);

    applyCompressor(buffer, chainSettings);

    updateFilters();
    
    juce::dsp::AudioBlock<float> block(buffer);
    
//    buffer.clear();
//
//    for( int i = 0; i < buffer.getNumSamples(); ++i )
//    {
//        buffer.setSample(0, i, osc.processSample(0));
//    }
//
//    juce::dsp::ProcessContextReplacing<float> stereoContext(block);
//    osc.process(stereoContext);
    
    auto leftBlock = block.getSingleChannelBlock(0);
    auto rightBlock = block.getSingleChannelBlock(1);
    
    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);
    
    leftChain.process(leftContext);
    rightChain.process(rightContext);

    applyDistortion(buffer, chainSettings);

    applyGain(buffer, chainSettings.outputGainInDecibels);

    bool didOutputClip = false;
    float outputPeak = 0.0f;
    for( int channel = 0; channel < buffer.getNumChannels(); ++channel )
    {
        const auto* channelData = buffer.getReadPointer(channel);
        for( int sample = 0; sample < buffer.getNumSamples(); ++sample )
        {
            const auto absSample = std::abs(channelData[sample]);
            outputPeak = juce::jmax(outputPeak, absSample);
            didOutputClip = didOutputClip || (absSample >= 1.0f);
        }
    }

    if( didOutputClip )
        outputClippingDetected.store(true, std::memory_order_release);

    pushPeakLevel(outputPeakLevel, outputPeak);
    
    leftChannelFifo.update(buffer);
    rightChannelFifo.update(buffer);
    
}

void SimpleEQAudioProcessor::applyGain(juce::AudioBuffer<float>& buffer, float gainDecibels)
{
    const auto gain = juce::Decibels::decibelsToGain(gainDecibels);
    buffer.applyGain(gain);
}

void SimpleEQAudioProcessor::pushPeakLevel(std::atomic<float>& targetPeak, float peakValue)
{
    auto previousPeak = targetPeak.load(std::memory_order_relaxed);
    while( peakValue > previousPeak
           && !targetPeak.compare_exchange_weak(previousPeak,
                                                peakValue,
                                                std::memory_order_release,
                                                std::memory_order_relaxed) )
    {
    }
}

//==============================================================================
bool SimpleEQAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SimpleEQAudioProcessor::createEditor()
{
    auto webViewOptions = juce::WebBrowserComponent::Options{}
                              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2);

    if (!juce::WebBrowserComponent::areOptionsSupported(webViewOptions))
        return new SimpleEQAudioProcessorEditor(*this);

    return new SimpleEQWebViewEditor(*this);
//    return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void SimpleEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    
    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);
}

void SimpleEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if( tree.isValid() )
    {
        apvts.replaceState(tree);
        updateFilters();
    }
}

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts)
{
    ChainSettings settings;
    
    settings.lowCutFreq = apvts.getRawParameterValue("LowCut Freq")->load();
    settings.highCutFreq = apvts.getRawParameterValue("HighCut Freq")->load();
    settings.peakFreq = apvts.getRawParameterValue("Peak Freq")->load();
    settings.peakGainInDecibels = apvts.getRawParameterValue("Peak Gain")->load();
    settings.peakQuality = apvts.getRawParameterValue("Peak Quality")->load();
    settings.inputGainInDecibels = apvts.getRawParameterValue("Input Gain")->load();
    settings.compressorAmount = apvts.getRawParameterValue("Compressor Amount")->load();
    settings.outputGainInDecibels = apvts.getRawParameterValue("Output Gain")->load();
    settings.distortionDriveInDecibels = apvts.getRawParameterValue("Distortion Drive")->load();
    settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("LowCut Slope")->load());
    settings.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("HighCut Slope")->load());
    
    settings.lowCutBypassed = apvts.getRawParameterValue("LowCut Bypassed")->load() > 0.5f;
    settings.peakBypassed = apvts.getRawParameterValue("Peak Bypassed")->load() > 0.5f;
    settings.highCutBypassed = apvts.getRawParameterValue("HighCut Bypassed")->load() > 0.5f;
    settings.distortionBypassed = apvts.getRawParameterValue("Distortion Bypassed")->load() > 0.5f;
    settings.compressorBypassed = apvts.getRawParameterValue("Compressor Bypassed")->load() > 0.5f;
    
    return settings;
}

void SimpleEQAudioProcessor::applyCompressor(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.compressorBypassed)
        return;

    const auto amount = juce::jlimit(0.0f, 24.0f, chainSettings.compressorAmount);
    if (amount <= 0.0f)
        return;

    const auto amountNorm = amount / 24.0f;

    const auto thresholdDb = juce::jmap(amountNorm, 0.0f, 1.0f, -6.0f, -30.0f);
    const auto ratio = juce::jmap(amountNorm, 0.0f, 1.0f, 1.2f, 8.0f);
    const auto attackMs = juce::jmap(amountNorm, 0.0f, 1.0f, 20.0f, 2.5f);
    const auto releaseMs = juce::jmap(amountNorm, 0.0f, 1.0f, 220.0f, 65.0f);

    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    const auto attackCoeff = std::exp(-1.0f / (0.001f * attackMs * static_cast<float>(sampleRate)));
    const auto releaseCoeff = std::exp(-1.0f / (0.001f * releaseMs * static_cast<float>(sampleRate)));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        auto& gainReductionDb = compressorGainReductionDb[static_cast<size_t>(juce::jmin(channel, 1))];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto detectorInput = std::abs(channelData[sample]) + 1.0e-9f;
            const auto detectorDb = juce::Decibels::gainToDecibels(detectorInput, -120.0f);
            const auto overDb = juce::jmax(0.0f, detectorDb - thresholdDb);

            const auto targetReductionDb = overDb > 0.0f
                                               ? overDb * (1.0f - (1.0f / ratio))
                                               : 0.0f;

            const auto smoothingCoeff = targetReductionDb > gainReductionDb ? attackCoeff : releaseCoeff;
            gainReductionDb = smoothingCoeff * gainReductionDb + (1.0f - smoothingCoeff) * targetReductionDb;

            const auto gain = juce::Decibels::decibelsToGain(-gainReductionDb);
            channelData[sample] *= gain;
        }
    }
}

void SimpleEQAudioProcessor::applyDistortion(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.distortionBypassed)
        return;

    const auto driveDb = chainSettings.distortionDriveInDecibels;
    const auto driveNorm = juce::jlimit(0.0f, 1.0f, driveDb / 24.0f);

    const auto preGain = juce::Decibels::decibelsToGain(3.0f + driveDb * 0.85f);
    const auto wetMix = juce::jmap(driveNorm, 0.0f, 1.0f, 0.30f, 0.92f);
    const auto autoGain = juce::jlimit(0.30f, 1.0f, std::pow(preGain, -0.28f));

    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    const auto cutoffHz = juce::jmap(driveNorm, 0.0f, 1.0f, 12000.0f, 4500.0f);
    const auto smoothing = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate));

    auto softClip = [](float x)
    {
        const auto ax = std::abs(x);
        if (ax <= 1.0f)
            return x - (x * x * x) / 3.0f;

        return (x > 0.0f ? 2.0f / 3.0f : -2.0f / 3.0f);
    };

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        auto& toneState = distortionToneState[static_cast<size_t>(juce::jmin(channel, 1))];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto dry = channelData[sample];
            const auto driven = dry * preGain;

            const auto tanhShape = std::tanh(driven);
            const auto cubicShape = softClip(driven * 0.75f) * 1.4f;
            auto wet = (0.78f * tanhShape + 0.22f * cubicShape) * autoGain;

            toneState += smoothing * (wet - toneState);
            wet = toneState;

            channelData[sample] = juce::jlimit(-1.5f, 1.5f, juce::jmap(wetMix, dry, wet));
        }
    }
}

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate,
                                                               chainSettings.peakFreq,
                                                               chainSettings.peakQuality,
                                                               juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels));
}

void SimpleEQAudioProcessor::updatePeakFilter(const ChainSettings &chainSettings)
{
    auto peakCoefficients = makePeakFilter(chainSettings, getSampleRate());
    
    leftChain.setBypassed<ChainPositions::Peak>(chainSettings.peakBypassed);
    rightChain.setBypassed<ChainPositions::Peak>(chainSettings.peakBypassed);
    
    updateCoefficients(leftChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    updateCoefficients(rightChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
}

void updateCoefficients(Coefficients &old, const Coefficients &replacements)
{
    *old = *replacements;
}

void SimpleEQAudioProcessor::updateLowCutFilters(const ChainSettings &chainSettings)
{
    auto cutCoefficients = makeLowCutFilter(chainSettings, getSampleRate());
    auto& leftLowCut = leftChain.get<ChainPositions::LowCut>();
    auto& rightLowCut = rightChain.get<ChainPositions::LowCut>();
    
    leftChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);
    rightChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);
    
    updateCutFilter(rightLowCut, cutCoefficients, chainSettings.lowCutSlope);
    updateCutFilter(leftLowCut, cutCoefficients, chainSettings.lowCutSlope);
}

void SimpleEQAudioProcessor::updateHighCutFilters(const ChainSettings &chainSettings)
{
    auto highCutCoefficients = makeHighCutFilter(chainSettings, getSampleRate());
    
    auto& leftHighCut = leftChain.get<ChainPositions::HighCut>();
    auto& rightHighCut = rightChain.get<ChainPositions::HighCut>();
    
    leftChain.setBypassed<ChainPositions::HighCut>(chainSettings.highCutBypassed);
    rightChain.setBypassed<ChainPositions::HighCut>(chainSettings.highCutBypassed);
    
    updateCutFilter(leftHighCut, highCutCoefficients, chainSettings.highCutSlope);
    updateCutFilter(rightHighCut, highCutCoefficients, chainSettings.highCutSlope);
}

void SimpleEQAudioProcessor::updateFilters()
{
    auto chainSettings = getChainSettings(apvts);
    
    updateLowCutFilters(chainSettings);
    updatePeakFilter(chainSettings);
    updateHighCutFilters(chainSettings);
}

juce::AudioProcessorValueTreeState::ParameterLayout SimpleEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("LowCut Freq",
                                                           "LowCut Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
                                                           20.f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("HighCut Freq",
                                                           "HighCut Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
                                                           20000.f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak Freq",
                                                           "Peak Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
                                                           750.f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak Gain",
                                                           "Peak Gain",
                                                           juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f),
                                                           0.0f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak Quality",
                                                           "Peak Quality",
                                                           juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f),
                                                           1.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Input Gain",
                                                           "Input Gain",
                                                           juce::NormalisableRange<float>(-24.f, 24.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Compressor Amount",
                                                           "Compressor Amount",
                                                           juce::NormalisableRange<float>(0.f, 24.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Output Gain",
                                                           "Output Gain",
                                                           juce::NormalisableRange<float>(-24.f, 24.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Distortion Drive",
                                                           "Distortion Drive",
                                                           juce::NormalisableRange<float>(0.f, 24.f, 0.1f, 1.f),
                                                           0.f));
    
    juce::StringArray stringArray;
    for( int i = 0; i < 4; ++i )
    {
        juce::String str;
        str << (12 + i*12);
        str << " db/Oct";
        stringArray.add(str);
    }
    
    layout.add(std::make_unique<juce::AudioParameterChoice>("LowCut Slope", "LowCut Slope", stringArray, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("HighCut Slope", "HighCut Slope", stringArray, 0));
    
    layout.add(std::make_unique<juce::AudioParameterBool>("LowCut Bypassed", "LowCut Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Peak Bypassed", "Peak Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("HighCut Bypassed", "HighCut Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Distortion Bypassed", "Distortion Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Compressor Bypassed", "Compressor Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Analyzer Enabled", "Analyzer Enabled", true));
    
    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleEQAudioProcessor();
}
