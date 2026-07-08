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
    
    for (auto& f : leftGraphicEq)
        f.prepare(spec);
    for (auto& f : rightGraphicEq)
        f.prepare(spec);

    updateFilters();
    
    leftChannelFifo.prepare(samplesPerBlock);
    rightChannelFifo.prepare(samplesPerBlock);

    osc.initialise([](float x) { return std::sin(x); });

    spec.numChannels = getTotalNumOutputChannels();
    osc.prepare(spec);
    osc.setFrequency(440);

    reverbParameters = {};
    reverb.setParameters(reverbParameters);
    reverb.prepare(spec);

    for (auto& shifter : octavePitchShifters)
    {
        shifter.prepare();
        shifter.setSampleRate(sampleRate);
    }

    for (auto& line : doublerDelayLines)
        line.prepare();
    for (auto& line : delayDelayLines)
        line.prepare();

    for (auto& shifter : doublerPitchShifters)
    {
        shifter.prepare();
        shifter.setSampleRate(sampleRate);
    }

    inputClippingDetected.store(false, std::memory_order_release);
    outputClippingDetected.store(false, std::memory_order_release);
    inputPeakLevel.store(0.0f, std::memory_order_release);
    outputPeakLevel.store(0.0f, std::memory_order_release);
    compressorGainReductionDb = { 0.0f, 0.0f };
    distortionToneState = { 0.0f, 0.0f };

    constexpr int tunerWindowSize = 2048;
    tunerYinBuffer.assign(static_cast<size_t>(tunerWindowSize), 0.0f);
    tunerAnalysisBuffer.assign(static_cast<size_t>(tunerWindowSize), 0.0f);
    tunerYinBufferWriteIndex = 0;
    tunerSamplesUntilNextAnalysis = tunerWindowSize;
    tunerAnalysisSampleRate = sampleRate;
    tunerDetectedFrequencyHz.store(0.0f, std::memory_order_release);
    tunerDetectedCents.store(0.0f, std::memory_order_release);
    tunerDetectedLevel.store(0.0f, std::memory_order_release);
    tunerDetectedNoteIndex.store(-1, std::memory_order_release);
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

    // In Mono Input mode, duplicate the left channel onto the right so a
    // mono source (guitar) plays out of both speakers and the delay/reverb
    // get a proper stereo image to work with.
    if (chainSettings.monoInput && buffer.getNumChannels() > 1)
        buffer.copyFrom(1, 0, buffer.getReadPointer(0), buffer.getNumSamples());

    applyGain(buffer, chainSettings.inputGainInDecibels);

    applyTuner(buffer, chainSettings);

    applyGate(buffer, chainSettings);

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

    auto leftBlock = block.getSingleChannelBlock(0);
    auto rightBlock = block.getSingleChannelBlock(1);

    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

    if (!chainSettings.graphicEqBypassed)
    {
        for (size_t i = 0; i < leftGraphicEq.size(); ++i)
        {
            leftGraphicEq[i].process(leftContext);
            rightGraphicEq[i].process(rightContext);
        }
    }

    applyOctave(buffer, chainSettings);

    applyDoubler(buffer, chainSettings);

    applyDistortion(buffer, chainSettings);

    applyFuzz(buffer, chainSettings);

    applyDelay(buffer, chainSettings);

    applyReverb(buffer, chainSettings);

    applyGain(buffer, chainSettings.outputGainInDecibels);

    // Global mute: silence the entire output. Applied last so it overrides
    // every effect in the chain.
    if (chainSettings.mute)
        buffer.clear();

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

void SimpleEQAudioProcessor::applyTuner(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const auto windowSize = static_cast<int>(tunerYinBuffer.size());
    if (windowSize <= 0)
        return;

    const auto referenceHz = juce::jlimit(430.0f, 450.0f, chainSettings.tunerReferencePitchHz);
    const auto halfBufferLevel = 1.0e-3f;

    auto rmsAccum = 0.0f;
    auto peak = 0.0f;
    const auto* leftData = buffer.getReadPointer(0);

    auto runAnalysis = [&](float blockRms, int blockSampleCount)
    {
        std::copy(tunerYinBuffer.begin(), tunerYinBuffer.end(), tunerAnalysisBuffer.begin());

        const auto analysisRms = std::sqrt(blockRms / static_cast<float>(juce::jmax(1, blockSampleCount)));
        tunerDetectedLevel.store(juce::Decibels::gainToDecibels(analysisRms, -60.0f), std::memory_order_release);

        if (analysisRms < halfBufferLevel)
        {
            tunerDetectedFrequencyHz.store(0.0f, std::memory_order_release);
            tunerDetectedCents.store(0.0f, std::memory_order_release);
            tunerDetectedNoteIndex.store(-1, std::memory_order_release);
            return;
        }

        // Autocorrelation-based pitch detection. YIN is tuned for voice and
        // doesn't cope well with guitar (where the fundamental is often
        // weaker than the 2nd harmonic), so we use plain normalized
        // autocorrelation and pick the lag with the highest peak, then apply
        // octave correction to prefer the lower octave when the signal
        // looks like it's landed on a harmonic.
        const auto fs = static_cast<float>(tunerAnalysisSampleRate);
        const int tauMax = windowSize / 2;
        std::vector<float> yin(static_cast<size_t>(tauMax), 0.0f);
        for (int tau = 1; tau < tauMax; ++tau)
        {
            auto sum = 0.0f;
            for (int j = 0; j < tauMax; ++j)
            {
                const auto delta = tunerAnalysisBuffer[static_cast<size_t>(j)]
                                 - tunerAnalysisBuffer[static_cast<size_t>(j + tau)];
                sum += delta * delta;
            }
            yin[static_cast<size_t>(tau)] = sum;
        }

        yin[0] = 1.0f;
        auto runningSum = 0.0f;
        for (int tau = 1; tau < tauMax; ++tau)
        {
            runningSum += yin[static_cast<size_t>(tau)];
            if (runningSum > 0.0f)
                yin[static_cast<size_t>(tau)] = yin[static_cast<size_t>(tau)] * static_cast<float>(tau) / runningSum;
            else
                yin[static_cast<size_t>(tau)] = 1.0f;
        }

        constexpr float yinThreshold = 0.15f;
        int tauEstimate = -1;
        for (int tau = 2; tau < tauMax; ++tau)
        {
            if (yin[static_cast<size_t>(tau)] < yinThreshold)
            {
                while (tau + 1 < tauMax && yin[static_cast<size_t>(tau + 1)] < yin[static_cast<size_t>(tau)])
                    ++tau;
                tauEstimate = tau;
                break;
            }
        }

        if (tauEstimate <= 0)
        {
            tunerDetectedFrequencyHz.store(0.0f, std::memory_order_release);
            tunerDetectedCents.store(0.0f, std::memory_order_release);
            tunerDetectedNoteIndex.store(-1, std::memory_order_release);
            return;
        }

        auto betterTau = static_cast<float>(tauEstimate);
        if (tauEstimate + 1 < tauMax && tauEstimate - 1 > 0)
        {
            const auto s0 = yin[static_cast<size_t>(tauEstimate - 1)];
            const auto s1 = yin[static_cast<size_t>(tauEstimate)];
            const auto s2 = yin[static_cast<size_t>(tauEstimate + 1)];
            const auto denom = 2.0f * (2.0f * s1 - s2 - s0);
            if (std::abs(denom) > 1.0e-9f)
                betterTau += (s2 - s0) / denom;
        }

        // Octave correction for guitar. If doubling the period (halving
        // the frequency) gives a comparable YIN value, the detected period
        // is likely a harmonic — prefer the lower octave.
        const auto tauOctaveDown = tauEstimate * 2;
        if (tauOctaveDown < tauMax
            && yin[static_cast<size_t>(tauEstimate)] < 0.15f
            && yin[static_cast<size_t>(tauOctaveDown)] < 0.4f)
        {
            auto octaveTau = static_cast<float>(tauOctaveDown);
            if (tauOctaveDown + 1 < tauMax && tauOctaveDown - 1 > 0)
            {
                const auto s0o = yin[static_cast<size_t>(tauOctaveDown - 1)];
                const auto s1o = yin[static_cast<size_t>(tauOctaveDown)];
                const auto s2o = yin[static_cast<size_t>(tauOctaveDown + 1)];
                const auto denomO = 2.0f * (2.0f * s1o - s2o - s0o);
                if (std::abs(denomO) > 1.0e-9f)
                    octaveTau += (s2o - s0o) / denomO;
            }
            betterTau = octaveTau;
        }

        const auto frequency = static_cast<float>(tunerAnalysisSampleRate) / betterTau;
        if (frequency < 20.0f || frequency > 5000.0f)
        {
            tunerDetectedFrequencyHz.store(0.0f, std::memory_order_release);
            tunerDetectedCents.store(0.0f, std::memory_order_release);
            tunerDetectedNoteIndex.store(-1, std::memory_order_release);
            return;
        }

        const auto noteNumberFloat = 12.0f * std::log2(frequency / referenceHz) + 69.0f;
        const auto noteNumber = static_cast<int>(std::lround(noteNumberFloat));
        const auto cents = (noteNumberFloat - static_cast<float>(noteNumber)) * 100.0f;

        tunerDetectedFrequencyHz.store(frequency, std::memory_order_release);
        tunerDetectedCents.store(cents, std::memory_order_release);
        tunerDetectedNoteIndex.store(noteNumber, std::memory_order_release);
    };

    auto samplesInBlockForRms = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto s = leftData[i];
        rmsAccum += s * s;
        peak = juce::jmax(peak, std::abs(s));
        ++samplesInBlockForRms;

        tunerYinBuffer[static_cast<size_t>(tunerYinBufferWriteIndex)] = s;
        tunerYinBufferWriteIndex = (tunerYinBufferWriteIndex + 1) % windowSize;

        if (--tunerSamplesUntilNextAnalysis <= 0)
        {
            tunerSamplesUntilNextAnalysis = windowSize;

            const auto blockRms = rmsAccum;
            rmsAccum = 0.0f;

            const auto blockSampleCount = samplesInBlockForRms;
            samplesInBlockForRms = 0;

            runAnalysis(blockRms, blockSampleCount);
        }
    }

    juce::ignoreUnused(peak);
}

void SimpleEQAudioProcessor::applyGate(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.gateBypassed)
        return;

    const auto thresholdDb = juce::jlimit(-60.0f, 0.0f, chainSettings.gateThresholdInDecibels);

    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    const auto attackMs = 0.5f;
    const auto releaseMs = 80.0f;
    const auto attackCoeff = std::exp(-1.0f / (0.001f * attackMs * static_cast<float>(sampleRate)));
    const auto releaseCoeff = std::exp(-1.0f / (0.001f * releaseMs * static_cast<float>(sampleRate)));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = buffer.getWritePointer(channel);
        auto& envelope = gateEnvelope[channelIndex];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto rawSample = channelData[sample];
            const auto detectorInput = std::abs(rawSample) + 1.0e-9f;
            const auto detectorDb = juce::Decibels::gainToDecibels(detectorInput, -120.0f);
            const auto isOpen = detectorDb >= thresholdDb ? 1.0f : 0.0f;

            const auto target = isOpen;
            const auto coeff = target > envelope ? attackCoeff : releaseCoeff;
            envelope = coeff * envelope + (1.0f - coeff) * target;

            channelData[sample] = rawSample * envelope;
        }
    }
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

    settings.inputGainInDecibels = apvts.getRawParameterValue("Input Gain")->load();
    settings.tunerReferencePitchHz = apvts.getRawParameterValue("Tuner Reference")->load();
    settings.gateThresholdInDecibels = apvts.getRawParameterValue("Gate Threshold")->load();
    settings.compressorAmount = apvts.getRawParameterValue("Compressor Amount")->load();
    settings.compressorTone = apvts.getRawParameterValue("Compressor Tone")->load();
    settings.compressorLevelInDecibels = apvts.getRawParameterValue("Compressor Level")->load();
    settings.octaveTransposeSemitones = apvts.getRawParameterValue("Octave Transpose")->load();
    settings.doublerMix = apvts.getRawParameterValue("Doubler Mix")->load();
    settings.doublerDelayMs = apvts.getRawParameterValue("Doubler Delay")->load();
    settings.doublerDetuneCents = apvts.getRawParameterValue("Doubler Detune")->load();
    settings.delayMix = apvts.getRawParameterValue("Delay Mix")->load();
    settings.delayTimeLMs = apvts.getRawParameterValue("Delay Time L")->load();
    settings.delayTimeRMs = apvts.getRawParameterValue("Delay Time R")->load();
    settings.delayFeedback = apvts.getRawParameterValue("Delay Feedback")->load();
    {
        const auto* modeParam = dynamic_cast<juce::AudioParameterChoice*>(
            apvts.getParameter("Delay Mode"));
        settings.delayModeIsDual = modeParam != nullptr && modeParam->getIndex() == 1;
    }
    settings.outputGainInDecibels = apvts.getRawParameterValue("Output Gain")->load();
    settings.distortionDriveInDecibels = apvts.getRawParameterValue("Distortion Drive")->load();
    settings.distortionTone = apvts.getRawParameterValue("Distortion Tone")->load();
    settings.distortionLevelInDecibels = apvts.getRawParameterValue("Distortion Level")->load();
    settings.fuzzDriveInDecibels = apvts.getRawParameterValue("Fuzz Drive")->load();
    settings.fuzzTone = apvts.getRawParameterValue("Fuzz Tone")->load();
    settings.fuzzLevelInDecibels = apvts.getRawParameterValue("Fuzz Level")->load();

    for (size_t i = 0; i < settings.graphicEqBandDb.size(); ++i)
    {
        const auto name = "EQ Band " + juce::String(static_cast<int>(i));
        settings.graphicEqBandDb[i] = apvts.getRawParameterValue(name)->load();
    }

    settings.reverbSize = apvts.getRawParameterValue("Reverb Size")->load();
    settings.reverbDamping = apvts.getRawParameterValue("Reverb Damping")->load();
    settings.reverbMix = apvts.getRawParameterValue("Reverb Mix")->load();
    settings.reverbWidth = apvts.getRawParameterValue("Reverb Width")->load();

    settings.graphicEqBypassed = apvts.getRawParameterValue("EQ Bypassed")->load() > 0.5f;
    settings.distortionBypassed = apvts.getRawParameterValue("Distortion Bypassed")->load() > 0.5f;
    settings.fuzzBypassed = apvts.getRawParameterValue("Fuzz Bypassed")->load() > 0.5f;
    settings.compressorBypassed = apvts.getRawParameterValue("Compressor Bypassed")->load() > 0.5f;
    settings.octaveBypassed = apvts.getRawParameterValue("Octave Bypassed")->load() > 0.5f;
    settings.doublerBypassed = apvts.getRawParameterValue("Doubler Bypassed")->load() > 0.5f;
    settings.delayBypassed = apvts.getRawParameterValue("Delay Bypassed")->load() > 0.5f;
    settings.monoInput = apvts.getRawParameterValue("Mono Input")->load() > 0.5f;
    settings.mute = apvts.getRawParameterValue("Mute")->load() > 0.5f;
    settings.reverbBypassed = apvts.getRawParameterValue("Reverb Bypassed")->load() > 0.5f;
    settings.tunerBypassed = apvts.getRawParameterValue("Tuner Bypassed")->load() > 0.5f;
    settings.gateBypassed = apvts.getRawParameterValue("Gate Bypassed")->load() > 0.5f;

    return settings;
}

void SimpleEQAudioProcessor::applyCompressor(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.compressorBypassed)
        return;

    const auto amount = juce::jlimit(0.0f, 24.0f, chainSettings.compressorAmount);
    if (amount <= 0.0f)
    {
        const auto makeup = juce::Decibels::decibelsToGain(chainSettings.compressorLevelInDecibels);
        buffer.applyGain(makeup);
        return;
    }

    const auto amountNorm = amount / 24.0f;

    const auto thresholdDb = juce::jmap(amountNorm, 0.0f, 1.0f, -6.0f, -30.0f);
    const auto ratio = juce::jmap(amountNorm, 0.0f, 1.0f, 1.2f, 8.0f);
    const auto attackMs = juce::jmap(amountNorm, 0.0f, 1.0f, 20.0f, 2.5f);
    const auto releaseMs = juce::jmap(amountNorm, 0.0f, 1.0f, 220.0f, 65.0f);
    const auto makeupGain = juce::Decibels::decibelsToGain(chainSettings.compressorLevelInDecibels);

    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    const auto attackCoeff = std::exp(-1.0f / (0.001f * attackMs * static_cast<float>(sampleRate)));
    const auto releaseCoeff = std::exp(-1.0f / (0.001f * releaseMs * static_cast<float>(sampleRate)));

    const auto toneNorm = juce::jlimit(0.0f, 1.0f, chainSettings.compressorTone);
    const auto sidechainCutoffHz = juce::jmap(toneNorm, 0.0f, 1.0f, 20.0f, 500.0f);
    const auto rc = 1.0f / (2.0f * juce::MathConstants<float>::pi * sidechainCutoffHz);
    const auto dt = 1.0f / static_cast<float>(sampleRate);
    compressorSidechainHpfCoeff = rc / (rc + dt);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = buffer.getWritePointer(channel);
        auto& gainReductionDb = compressorGainReductionDb[channelIndex];
        auto& hpfX1 = compressorSidechainHpfX1[channelIndex];
        auto& hpfY1 = compressorSidechainHpfY1[channelIndex];
        const auto hpfA = compressorSidechainHpfCoeff;

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto rawSample = channelData[sample];
            const auto detectorInputRaw = std::abs(rawSample) + 1.0e-9f;

            hpfY1 = hpfA * (hpfY1 + detectorInputRaw - hpfX1);
            hpfX1 = detectorInputRaw;
            const auto detectorInput = hpfY1 + 1.0e-9f;

            const auto detectorDb = juce::Decibels::gainToDecibels(detectorInput, -120.0f);
            const auto overDb = juce::jmax(0.0f, detectorDb - thresholdDb);

            const auto targetReductionDb = overDb > 0.0f
                                               ? overDb * (1.0f - (1.0f / ratio))
                                               : 0.0f;

            const auto smoothingCoeff = targetReductionDb > gainReductionDb ? attackCoeff : releaseCoeff;
            gainReductionDb = smoothingCoeff * gainReductionDb + (1.0f - smoothingCoeff) * targetReductionDb;

            const auto gain = juce::Decibels::decibelsToGain(-gainReductionDb) * makeupGain;
            channelData[sample] = rawSample * gain;
        }
    }
}

void SimpleEQAudioProcessor::applyOctave(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.octaveBypassed)
        return;

    const auto semitones = juce::jlimit(-12.0f, 12.0f, chainSettings.octaveTransposeSemitones);
    if (semitones == 0.0f)
        return;

    const auto pitchRatio = std::pow(2.0f, semitones / 12.0f);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = buffer.getWritePointer(channel);
        auto& shifter = octavePitchShifters[channelIndex];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            channelData[sample] = shifter.processSample(channelData[sample], pitchRatio);
    }
}

void SimpleEQAudioProcessor::applyDoubler(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.doublerBypassed)
        return;

    const auto mix = juce::jlimit(0.0f, 1.0f, chainSettings.doublerMix);
    if (mix <= 0.0f)
        return;

    const auto sampleRate = static_cast<float>(juce::jmax(1.0, getSampleRate()));
    const auto delaySamples = juce::jlimit(0.0f, static_cast<float>(DelayLine::size - 2),
                                            chainSettings.doublerDelayMs * sampleRate / 1000.0f);
    const auto detuneRatio = std::pow(2.0f, chainSettings.doublerDetuneCents / 1200.0f);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = buffer.getWritePointer(channel);
        auto& delayLine = doublerDelayLines[channelIndex];
        auto& shifter = doublerPitchShifters[channelIndex];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto dry = channelData[sample];

            delayLine.push(dry);
            const auto delayed = delayLine.read(delaySamples);
            const auto detuned = shifter.processSample(delayed, detuneRatio);

            channelData[sample] = dry * (1.0f - mix) + detuned * mix;
        }
    }
}

void SimpleEQAudioProcessor::applyDelay(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.delayBypassed)
        return;

    const auto mix = juce::jlimit(0.0f, 1.0f, chainSettings.delayMix);
    if (mix <= 0.0f)
        return;

    const auto sampleRate = static_cast<float>(juce::jmax(1.0, getSampleRate()));
    const auto maxDelaySamples = static_cast<float>(DelayLine::size - 2);
    const auto timeLSamples = juce::jlimit(0.0f, maxDelaySamples,
                                            chainSettings.delayTimeLMs * sampleRate / 1000.0f);
    const auto timeRSamples = juce::jlimit(0.0f, maxDelaySamples,
                                            chainSettings.delayTimeRMs * sampleRate / 1000.0f);
    const auto feedback = juce::jlimit(0.0f, 0.95f, chainSettings.delayFeedback);

    const auto delays = chainSettings.delayModeIsDual
        ? std::array<float, 2> { timeLSamples, timeRSamples }
        : std::array<float, 2> { timeLSamples, timeLSamples };

    // For a mono source the right channel arrives silent, which would keep the
    // ch1 delay line permanently at 0. Feed ch1's delay line from the left
    // channel so the right-channel delay still works in Dual mode.
    auto* ch0Data = buffer.getReadPointer(0);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = buffer.getWritePointer(channel);
        auto& line = delayDelayLines[channelIndex];
        const auto delaySamples = delays[channelIndex];

        const auto* feed = (channel == 1) ? ch0Data : channelData;

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto dry = channelData[sample];
            const auto wet = line.read(delaySamples);
            const auto nextWet = feed[sample] + wet * feedback;
            line.push(nextWet);
            channelData[sample] = dry * (1.0f - mix) + wet * mix;
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
    const auto levelGain = juce::Decibels::decibelsToGain(chainSettings.distortionLevelInDecibels);

    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    const auto toneNorm = juce::jlimit(0.0f, 1.0f, chainSettings.distortionTone);
    const auto cutoffHz = juce::jmap(toneNorm, 0.0f, 1.0f, 1000.0f, 10000.0f);
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
            wet = toneState * levelGain;

            channelData[sample] = juce::jlimit(-1.5f, 1.5f, juce::jmap(wetMix, dry, wet));
        }
    }
}

void SimpleEQAudioProcessor::applyFuzz(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.fuzzBypassed)
        return;

    const auto driveDb = chainSettings.fuzzDriveInDecibels;
    const auto driveNorm = juce::jlimit(0.0f, 1.0f, driveDb / 24.0f);

    const auto preGain = juce::Decibels::decibelsToGain(4.0f + driveDb * 1.1f);
    const auto wetMix = juce::jmap(driveNorm, 0.0f, 1.0f, 0.50f, 1.0f);
    const auto octaveBlend = juce::jmap(driveNorm, 0.0f, 1.0f, 0.0f, 0.35f);
    const auto autoGain = juce::jlimit(0.20f, 1.0f, std::pow(preGain, -0.35f));
    const auto levelGain = juce::Decibels::decibelsToGain(chainSettings.fuzzLevelInDecibels);

    constexpr float positiveThreshold = 0.6f;
    constexpr float negativeThreshold = 0.4f;

    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    const auto toneNorm = juce::jlimit(0.0f, 1.0f, chainSettings.fuzzTone);
    const auto cutoffHz = juce::jmap(toneNorm, 0.0f, 1.0f, 1000.0f, 10000.0f);
    const auto smoothing = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        auto& toneState = fuzzToneState[static_cast<size_t>(juce::jmin(channel, 1))];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto dry = channelData[sample];
            const auto driven = dry * preGain;

            const auto asymmetric = driven >= 0.0f
                                        ? std::min(driven, positiveThreshold)
                                        : std::max(driven, -negativeThreshold);

            const auto rectified = std::abs(asymmetric) * 0.6f;
            auto wet = ((1.0f - octaveBlend) * asymmetric + octaveBlend * rectified) * autoGain;

            toneState += smoothing * (wet - toneState);
            wet = toneState * levelGain;

            channelData[sample] = juce::jlimit(-1.5f, 1.5f, juce::jmap(wetMix, dry, wet));
        }
    }
}

void SimpleEQAudioProcessor::applyReverb(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.reverbBypassed)
        return;

    const auto size = juce::jlimit(0.0f, 1.0f, chainSettings.reverbSize);
    const auto damping = juce::jlimit(0.0f, 1.0f, chainSettings.reverbDamping);
    const auto width = juce::jlimit(0.0f, 1.0f, chainSettings.reverbWidth);
    const auto mix = juce::jlimit(0.0f, 1.0f, chainSettings.reverbMix);

    if (mix <= 0.0f)
        return;

    reverbParameters.roomSize = size;
    reverbParameters.damping = damping;
    reverbParameters.width = width;
    reverbParameters.wetLevel = mix;
    reverbParameters.dryLevel = 1.0f - (mix * 0.5f);
    reverbParameters.freezeMode = 0.0f;

    reverb.setParameters(reverbParameters);

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    reverb.process(context);
}

// ISO-standard 1-octave graphic EQ centre frequencies, 31.25 Hz .. 16 kHz.
static constexpr std::array<float, 10> graphicEqFrequencies {
    31.25f, 62.5f, 125.0f, 250.0f, 500.0f,
    1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};

// Constant-Q for 1-octave spacing: Q = 1 / sin(pi/4) ~= 1.4142.
static constexpr float graphicEqQ = 1.41421356f;

void updateCoefficients(Coefficients &old, const Coefficients &replacements)
{
    *old = *replacements;
}

void SimpleEQAudioProcessor::updateGraphicEq(const ChainSettings& chainSettings)
{
    const auto sampleRate = getSampleRate();
    const bool bypassed = chainSettings.graphicEqBypassed;

    for (size_t i = 0; i < graphicEqFrequencies.size(); ++i)
    {
        const auto gainDb = bypassed ? 0.0f : chainSettings.graphicEqBandDb[i];
        const auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate, graphicEqFrequencies[i], graphicEqQ,
            juce::Decibels::decibelsToGain(gainDb));

        updateCoefficients(leftGraphicEq[i].coefficients, coeffs);
        updateCoefficients(rightGraphicEq[i].coefficients, coeffs);
    }
}

void SimpleEQAudioProcessor::updateFilters()
{
    updateGraphicEq(getChainSettings(apvts));
}

juce::AudioProcessorValueTreeState::ParameterLayout SimpleEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    for (size_t i = 0; i < 10; ++i)
    {
        const auto name = "EQ Band " + juce::String(static_cast<int>(i));
        layout.add(std::make_unique<juce::AudioParameterFloat>(name,
                                                               name,
                                                               juce::NormalisableRange<float>(-12.f, 12.f, 0.1f, 1.f),
                                                               0.0f));
    }

    layout.add(std::make_unique<juce::AudioParameterFloat>("Input Gain",
                                                           "Input Gain",
                                                           juce::NormalisableRange<float>(-24.f, 24.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Tuner Reference",
                                                           "Tuner Reference",
                                                           juce::NormalisableRange<float>(430.f, 450.f, 0.1f, 1.f),
                                                           440.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Gate Threshold",
                                                           "Gate Threshold",
                                                           juce::NormalisableRange<float>(-60.f, 0.f, 0.1f, 1.f),
                                                           -60.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Compressor Amount",
                                                           "Compressor Amount",
                                                           juce::NormalisableRange<float>(0.f, 24.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Compressor Tone",
                                                           "Compressor Tone",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Compressor Level",
                                                           "Compressor Level",
                                                           juce::NormalisableRange<float>(-12.f, 24.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Octave Transpose",
                                                           "Octave Transpose",
                                                           juce::NormalisableRange<float>(-12.f, 12.f, 1.f, 1.f),
                                                           0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Doubler Mix",
                                                           "Doubler Mix",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Doubler Delay",
                                                           "Doubler Delay",
                                                           juce::NormalisableRange<float>(0.f, 100.f, 0.5f, 1.f),
                                                           20.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Doubler Detune",
                                                           "Doubler Detune",
                                                           juce::NormalisableRange<float>(-50.f, 50.f, 1.f, 1.f),
                                                           5.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Delay Mix",
                                                           "Delay Mix",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.35f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Delay Time L",
                                                           "Delay Time L",
                                                           juce::NormalisableRange<float>(0.f, 2000.f, 1.f, 1.f),
                                                           350.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Delay Time R",
                                                           "Delay Time R",
                                                           juce::NormalisableRange<float>(0.f, 2000.f, 1.f, 1.f),
                                                           350.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Delay Feedback",
                                                           "Delay Feedback",
                                                           juce::NormalisableRange<float>(0.f, 0.95f, 0.01f, 1.f),
                                                           0.35f));

    layout.add(std::make_unique<juce::AudioParameterChoice>("Delay Mode",
                                                            "Delay Mode",
                                                            juce::StringArray { "Single", "Dual" },
                                                            0));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Output Gain",
                                                           "Output Gain",
                                                           juce::NormalisableRange<float>(-24.f, 24.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Distortion Drive",
                                                           "Distortion Drive",
                                                           juce::NormalisableRange<float>(0.f, 24.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Distortion Tone",
                                                           "Distortion Tone",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.7f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Distortion Level",
                                                           "Distortion Level",
                                                           juce::NormalisableRange<float>(-24.f, 12.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Fuzz Drive",
                                                           "Fuzz Drive",
                                                           juce::NormalisableRange<float>(0.f, 24.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Fuzz Tone",
                                                           "Fuzz Tone",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.7f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Fuzz Level",
                                                           "Fuzz Level",
                                                           juce::NormalisableRange<float>(-24.f, 12.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Reverb Size",
                                                           "Reverb Size",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Reverb Damping",
                                                           "Reverb Damping",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Reverb Mix",
                                                           "Reverb Mix",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Reverb Width",
                                                           "Reverb Width",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           1.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>("EQ Bypassed", "EQ Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Distortion Bypassed", "Distortion Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Fuzz Bypassed", "Fuzz Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Compressor Bypassed", "Compressor Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Octave Bypassed", "Octave Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Doubler Bypassed", "Doubler Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Delay Bypassed", "Delay Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Mono Input", "Mono Input", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Mute", "Mute", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Reverb Bypassed", "Reverb Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Tuner Bypassed", "Tuner Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Gate Bypassed", "Gate Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Analyzer Enabled", "Analyzer Enabled", true));

    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleEQAudioProcessor();
}
