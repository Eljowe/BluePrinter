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

    distOversampler.initProcessing(samplesPerBlock);
    fuzzOversampler.initProcessing(samplesPerBlock);
    overdriveOversampler.initProcessing(samplesPerBlock);

    // Allocate the dry-snapshot scratch on the message thread. Both
    // applyDistortion and applyFuzz copy their input into this buffer
    // at the start of the wet path and read from it for the final
    // dry/wet crossfade.
    dryScratchBuffer.setSize (juce::jmax (1, getTotalNumOutputChannels()),
                              samplesPerBlock);

    // Distortion and fuzz are oversampled in series; report the combined
    // filter group delay so the host can compensate track-to-track.
    updateLatency();

    for (auto& shifter : octavePitchShifters)
    {
        shifter.prepare();
        shifter.setSampleRate(sampleRate);
    }
    for (auto& line : octavePadDelay)
        line.prepare();

    for (auto& line : doublerDelayLines)
        line.prepare();
    for (auto& line : delayDelayLines)
        line.prepare();

    for (auto& shifter : doublerPitchShifters)
    {
        shifter.prepare();
        shifter.setSampleRate(sampleRate);
    }

    for (auto& line : synthFuzzDelayLines)
        line.prepare();
    for (auto& shifter : synthFuzzPitchShifters)
    {
        shifter.prepare();
        shifter.setSampleRate(sampleRate);
    }
    synthFuzzHpfState = { 0.0f, 0.0f };

    inputClippingDetected.store(false, std::memory_order_release);
    outputClippingDetected.store(false, std::memory_order_release);
    inputPeakLevel.store(0.0f, std::memory_order_release);
    outputPeakLevel.store(0.0f, std::memory_order_release);
    compressorGainReductionDb = { 0.0f, 0.0f };
    distortionToneState = { 0.0f, 0.0f };
    overdriveToneState = { 0.0f, 0.0f };
    distortionPreHpfX1 = { 0.0f, 0.0f };
    distortionPreHpfY1 = { 0.0f, 0.0f };
    distortionDcBlockX1 = { 0.0f, 0.0f };
    distortionDcBlockY1 = { 0.0f, 0.0f };
    fuzzPreHpfX1 = { 0.0f, 0.0f };
    fuzzPreHpfY1 = { 0.0f, 0.0f };
    fuzzDcBlockX1 = { 0.0f, 0.0f };
    fuzzDcBlockY1 = { 0.0f, 0.0f };
    fuzzBiasFollower = { 0.0f, 0.0f };
    octaveLastInput = { 0.0f, 0.0f };
    octaveFlipFlopState = { false, false };
    octaveTrackingFilterState = { 0.0f, 0.0f };
    octaveOutputLpfState = { 0.0f, 0.0f };
    octaveEnvState = { 0.0f, 0.0f };
    octaveHoldCounter = { 0, 0 };
    octaveDcBlockX1 = { 0.0f, 0.0f };
    octaveDcBlockY1 = { 0.0f, 0.0f };
    overdrivePreHpfX1 = { 0.0f, 0.0f };
    overdrivePreHpfY1 = { 0.0f, 0.0f };
    overdriveDcBlockX1 = { 0.0f, 0.0f };
    overdriveDcBlockY1 = { 0.0f, 0.0f };

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

void SimpleEQAudioProcessor::updateLatency()
{
    // Oversampler delays are fixed and accumulate in series. The octave
    // stage's pitch shifter has a data-dependent natural latency
    // (grainLength / pitchRatio, 1024..4096 samples across the +/-12 st
    // range), so we always run the shifter and pad its output with a
    // matching fractional delay so the stage's total contribution is a
    // constant 4096 samples. That gives the host a stable number it can
    // compensate against without per-sample surprises when the user
    // moves the transpose knob.
    //
    // Called once from prepareToPlay. Both summands below are
    // deterministic compile-time constants after prepareToPlay has
    // initialised the oversamplers, so the result is bit-stable across
    // processBlock calls -- the host can rely on it without re-querying.
    const auto oversamplerLatency = overdriveOversampler.getLatencyInSamples()
                                  + distOversampler.getLatencyInSamples()
                                  + fuzzOversampler.getLatencyInSamples();
    // kOctaveStageLatencySamples is the worst-case grain lookback the
    // octave stage contributes; derived from the transpose range and
    // the shifter's grain length. See applyOctave() for the matching
    // derivation.
    constexpr int kOctaveStageLatencySamples = kOctaveWorstCaseLookback;

    setLatencySamples(static_cast<int>(oversamplerLatency) + kOctaveStageLatencySamples);
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

    applySynthFuzz(buffer, chainSettings);

    applyOverdrive(buffer, chainSettings);

    applyDistortion(buffer, chainSettings);

    applyFuzz(buffer, chainSettings);

    applyDelay(buffer, chainSettings);

    applyReverb(buffer, chainSettings);

    applyTremolo(buffer, chainSettings);

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
    settings.octaveMix = apvts.getRawParameterValue("Octave Mix")->load();
    settings.octaveLevelInDecibels = apvts.getRawParameterValue("Octave Level")->load();
    settings.octaveTone = apvts.getRawParameterValue("Octave Tone")->load();
    settings.octaveMonoDetector = apvts.getRawParameterValue("Octave Mono Detector")->load() > 0.5f;
    settings.doublerMix = apvts.getRawParameterValue("Doubler Mix")->load();
    settings.doublerDelayMs = apvts.getRawParameterValue("Doubler Delay")->load();
    settings.doublerDetuneCents = apvts.getRawParameterValue("Doubler Detune")->load();
    settings.synthFuzzMix = apvts.getRawParameterValue("Synth Fuzz Mix")->load();
    settings.synthFuzzDelayMs = apvts.getRawParameterValue("Synth Fuzz Delay")->load();
    settings.synthFuzzDetuneCents = apvts.getRawParameterValue("Synth Fuzz Detune")->load();
    settings.synthFuzzDrive = apvts.getRawParameterValue("Synth Fuzz Drive")->load();
    settings.synthFuzzLevelInDecibels = apvts.getRawParameterValue("Synth Fuzz Level")->load();
    settings.tremoloSpeedHz = apvts.getRawParameterValue("Tremolo Speed")->load();
    settings.tremoloDepth = apvts.getRawParameterValue("Tremolo Depth")->load();
    {
        const auto* lfoParam = dynamic_cast<juce::AudioParameterChoice*>(
            apvts.getParameter("Tremolo LFO"));
        settings.tremoloLfoIndex = lfoParam != nullptr ? lfoParam->getIndex() : 0;
    }
    settings.tremoloStereoPhase = apvts.getRawParameterValue("Tremolo Stereo Phase")->load() > 0.5f;
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
    settings.overdriveDriveInDecibels = apvts.getRawParameterValue("Overdrive Drive")->load();
    settings.overdriveTone = apvts.getRawParameterValue("Overdrive Tone")->load();
    settings.overdriveLevelInDecibels = apvts.getRawParameterValue("Overdrive Level")->load();
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
    settings.overdriveBypassed = apvts.getRawParameterValue("Overdrive Bypassed")->load() > 0.5f;
    settings.distortionBypassed = apvts.getRawParameterValue("Distortion Bypassed")->load() > 0.5f;
    settings.fuzzBypassed = apvts.getRawParameterValue("Fuzz Bypassed")->load() > 0.5f;
    settings.compressorBypassed = apvts.getRawParameterValue("Compressor Bypassed")->load() > 0.5f;
    settings.octaveBypassed = apvts.getRawParameterValue("Octave Bypassed")->load() > 0.5f;
    settings.doublerBypassed = apvts.getRawParameterValue("Doubler Bypassed")->load() > 0.5f;
    settings.synthFuzzBypassed = apvts.getRawParameterValue("Synth Fuzz Bypassed")->load() > 0.5f;
    settings.tremoloBypassed = apvts.getRawParameterValue("Tremolo Bypassed")->load() > 0.5f;
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
    const auto makeupGain = juce::Decibels::decibelsToGain(chainSettings.compressorLevelInDecibels);

    if (amount <= 0.0f)
    {
        buffer.applyGain(makeupGain);
        return;
    }

    const auto amountNorm = amount / 24.0f;
    const auto thresholdDb = juce::jmap(amountNorm, 0.0f, 1.0f, -6.0f, -30.0f);
    const auto ratio = juce::jmap(amountNorm, 0.0f, 1.0f, 1.2f, 8.0f);
    const auto attackMs = juce::jmap(amountNorm, 0.0f, 1.0f, 20.0f, 2.5f);
    const auto releaseMs = juce::jmap(amountNorm, 0.0f, 1.0f, 220.0f, 65.0f);

    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    const auto attackCoeff = std::exp(-1.0f / (0.001f * attackMs * static_cast<float>(sampleRate)));
    const auto releaseCoeff = std::exp(-1.0f / (0.001f * releaseMs * static_cast<float>(sampleRate)));

    // Set up the Sidechain HPF coefficient based on Tone control
    const auto toneNorm = juce::jlimit(0.0f, 1.0f, chainSettings.compressorTone);
    const auto sidechainCutoffHz = juce::jmap(toneNorm, 0.0f, 1.0f, 20.0f, 500.0f);
    const auto rc = 1.0f / (2.0f * juce::MathConstants<float>::pi * sidechainCutoffHz);
    const auto dt = 1.0f / static_cast<float>(sampleRate);
    const auto hpfA = rc / (rc + dt);

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();

    // Loop through samples first, ensuring true stereo linking
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float maxDetectorInput = 0.0f;

        // Step 1: Filter raw channels, then grab the maximum rectified peak across all channels
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
            const auto rawSample = buffer.getSample(channel, sample);

            auto& hpfX1 = compressorSidechainHpfX1[channelIndex];
            auto& hpfY1 = compressorSidechainHpfY1[channelIndex];

            // Correct order: HPF on raw signal first!
            hpfY1 = hpfA * (hpfY1 + rawSample - hpfX1);
            hpfX1 = rawSample;

            // Capture the absolute amplitude of the filtered signal
            const float rectSample = std::abs(hpfY1);
            if (rectSample > maxDetectorInput)
                maxDetectorInput = rectSample;
        }

        // Add small epsilon to prevent log10(0) safety issues
        maxDetectorInput += 1.0e-9f;

        // Step 2: Convert combined peak to Decibels and find compression target
        const auto detectorDb = juce::Decibels::gainToDecibels(maxDetectorInput, -120.0f);
        const auto overDb = juce::jmax(0.0f, detectorDb - thresholdDb);

        const auto targetReductionDb = overDb > 0.0f
                                       ? overDb * (1.0f - (1.0f / ratio))
                                       : 0.0f;

        // Step 3: Smooth the gain envelope using a single tracker shared across channels
        // Note: For true stereo tracking, ensure 'compressorGainReductionDb[0]' is used as a unified envelope
        auto& sharedGainReductionDb = compressorGainReductionDb[0];
        
        const auto smoothingCoeff = targetReductionDb > sharedGainReductionDb ? attackCoeff : releaseCoeff;
        sharedGainReductionDb = smoothingCoeff * sharedGainReductionDb + (1.0f - smoothingCoeff) * targetReductionDb;

        // Step 4: Apply identical gain reduction and makeup gain to every channel
        const auto appliedGain = juce::Decibels::decibelsToGain(-sharedGainReductionDb) * makeupGain;
        for (int channel = 0; channel < numChannels; ++channel)
        {
            buffer.setSample(channel, sample, buffer.getSample(channel, sample) * appliedGain);
        }
    }
}

void SimpleEQAudioProcessor::applyOctave(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.octaveBypassed)
        return;

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const auto mixNorm  = juce::jlimit(0.0f, 1.0f, chainSettings.octaveMix);
    const auto levelGain = juce::Decibels::decibelsToGain(chainSettings.octaveLevelInDecibels);
    const auto toneNorm  = juce::jlimit(0.0f, 1.0f, chainSettings.octaveTone);
    const bool monoDetector = chainSettings.octaveMonoDetector;

    // Transpose shifts the generated sub voice by N semitones. The engine
    // always produces a -1 octave square wave, so the base pitchRatio is 0.5
    // and we apply 2^(st/12) on top of that. At transpose=0 the result is
    // the unmodified sub. At +/-12 the sub moves to 0 or -2 octaves.
    // The transpose range and the worst-case lookback are defined
    // together in the header (kOctaveTranspose{Min,Max}Semitones and
    // kOctaveWorstCaseLookback) so the APVTS layout and the latency
    // report can never drift out of sync. If the APVTS range widens,
    // update the header constants in the same change.
    const auto transposeSemitones = juce::jlimit(kOctaveTransposeMinSemitones,
                                                 kOctaveTransposeMaxSemitones,
                                                 chainSettings.octaveTransposeSemitones);
    const float pitchRatio = std::pow(2.0f, transposeSemitones / 12.0f) * 0.5f;

    // The granular pitch shifter has a grain lookback of
    // grainLength / pitchRatio, which is data-dependent (1024..4096
    // samples across the +/-12 st range). To keep the plugin's total
    // output latency constant — so the host's PDC stays correct and
    // there's no click on knob change — we always run the shifter and
    // pad the rest with a fractional delay line (DelayLine::read() does
    // linear interpolation, so the pad amount can change smoothly
    // sample-by-sample) so the shifter+pad always equals
    // kOctaveWorstCaseLookback. updateLatency() reports that same fixed
    // number, and the per-channel octavePadDelay is a 262144-sample
    // ring buffer (DelayLine::size) so it has plenty of headroom for
    // the 4096-sample worst case.
    //
    // Clamp the ratio to [2^(Min/12) * 0.5, 1.0] -- with the current
    // +/-12 st range the upper bound of 1.0 is what we actually hit at
    // the +12 extreme, and the lower bound of 0.25 matches the ratio
    // the worst-case lookback was derived from. The value is hardcoded
    // (rather than computed via std::pow) because std::pow is not
    // constexpr until C++23 and we want a compile-time constant for
    // the clamp.
    //
    // If this fires, kMinPitchRatio is out of sync with
    // kOctaveTransposeMinSemitones -- recompute the literal above
    // (2^(Min/12) * 0.5) and update both together.
    static_assert(kOctaveTransposeMinSemitones == -12.0f,
                  "kMinPitchRatio (0.25f) assumes -12 semitones; "
                  "update both the literal and kOctaveTransposeMinSemitones together");
    constexpr float kMinPitchRatio = 0.25f; // 2^(-12/12) * 0.5
    const float clampedRatio = juce::jlimit(kMinPitchRatio, 1.0f, pitchRatio);
    const float shifterLookback =
        static_cast<float>(GranularPitchShifter::grainLength) / clampedRatio;
    const float octaveShifterPadSamples =
        static_cast<float>(kOctaveWorstCaseLookback) - shifterLookback;

    // Scratch-buffer safety: make sure the dry snapshot is large enough before
    // we write into it. prepareToPlay pre-sizes it, but a host reconfigure
    // could change the channel count.
    if (dryScratchBuffer.getNumChannels() < numChannels
        || dryScratchBuffer.getNumSamples() < numSamples)
    {
        dryScratchBuffer.setSize(numChannels, numSamples);
    }
    for (int ch = 0; ch < numChannels; ++ch)
        dryScratchBuffer.copyFrom(ch, 0, buffer.getReadPointer(ch), numSamples);

    // --- Engine coefficients -------------------------------------------------
    const auto sampleRate = static_cast<float>(juce::jmax(1.0, getSampleRate()));
    constexpr float trackingCutoffHz   = 120.0f;  // Pre-ZeroX LPF, keeps tracking clean
    constexpr float hysteresisBand     = 0.005f;  // Schmitt-trigger band on the LPF output
    constexpr float envAttackMs        = 8.0f;    // Snappy attack so transients speak
    constexpr float envReleaseMs       = 90.0f;   // Slow release for a musical decay
    constexpr float holdMs             = 60.0f;   // Pin envelope for this long after a hit

    const auto trackingLpfCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi
                                                 * trackingCutoffHz / sampleRate);
    const auto attackCoeff  = std::exp(-1.0f / (0.001f * envAttackMs  * sampleRate));
    const auto releaseCoeff = std::exp(-1.0f / (0.001f * envReleaseMs * sampleRate));
    const int  holdSamples  = static_cast<int>(0.001f * holdMs * sampleRate);

    // Output LPF cutoff is the sub-voice tone knob: 200 Hz (pure sub) -> 1500 Hz
    // (with harmonics still audible). Separate from the tracking LPF.
    const auto outputCutoffHz = juce::jmap(toneNorm, 0.0f, 1.0f, 200.0f, 1500.0f);
    const auto outputLpfCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi
                                               * outputCutoffHz / sampleRate);

    // DC blocker on the wet path tightens the low end of the squared sub.
    constexpr float dcBlockCoeff = 0.995f;

    // Equal-power crossfade gains (cos/sin pair sums to unity power).
    // At mixNorm=0: dry=1, wet=0. At mixNorm=1: dry=0, wet=1. Sum of
    // squares is always 1, so the perceived loudness stays constant.
    const auto mixAngle = mixNorm * juce::MathConstants<float>::halfPi;
    const auto dryGain  = std::cos(mixAngle);
    const auto wetGain  = std::sin(mixAngle);

    // Engine state always lives in slot 0. Per-channel output filtering stays
    // in slots 0/1 so that stereo widening can still happen on the wet side.
    constexpr size_t engineIndex = 0;
    auto& trackingLpf  = octaveTrackingFilterState[engineIndex];
    auto& lastInput    = octaveLastInput[engineIndex];
    auto& flipFlop     = octaveFlipFlopState[engineIndex];
    auto& envState     = octaveEnvState[engineIndex];
    auto& holdCounter  = octaveHoldCounter[engineIndex];

    // Mono-sum cache: when monoDetector is on we still need both L and R
    // samples to form the sum each iteration. Pull them up-front.
    const auto* leftRead  = buffer.getReadPointer(0);
    const auto* rightRead = numChannels > 1 ? buffer.getReadPointer(1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // 1. Detector input. Mono-summing matches a classic pedal where the
        //    engine listens to L+R, so the sub locks once per period of the
        //    summed signal. In stereo-detect mode we feed the engine from
        //    channel 0 only (the L/R widening happens after).
        const float detector = (monoDetector && rightRead != nullptr)
                                  ? 0.5f * (leftRead[sample] + rightRead[sample])
                                  : leftRead[sample];

        // 2. Tracking LPF. A tiny DC offset is added each sample so the
        //    recursive state can't drift into the denormal range while
        //    input is silent.
        trackingLpf += trackingLpfCoeff * (detector - trackingLpf) + 1.0e-25f;

        // 3. Schmitt-trigger zero-cross detector. The flip-flop toggles on
        //    rising edges only (half-rate -> sub-octave). Hysteresis around
        //    the band means noisy signals near zero don't chatter.
        if (lastInput <= +hysteresisBand && trackingLpf > +hysteresisBand)
            flipFlop = !flipFlop;
        lastInput = trackingLpf;

        // 4. Square-wave sub voice. Multiplied by the envelope so it tracks
        //    playing dynamics rather than sitting at a fixed amplitude.
        const float subSquare = flipFlop ? 1.0f : -1.0f;

        // 5. Asymmetric attack/release envelope follower. Attack is fast
        //    enough to catch pick transients; release is slow so a held
        //    note doesn't decay into a stuttering sub.
        const float inputAbs = std::abs(trackingLpf);
        const float envCoeff = (inputAbs > envState) ? attackCoeff : releaseCoeff;
        envState = envCoeff * envState + (1.0f - envCoeff) * inputAbs + 1.0e-25f;

        // 6. Hold smoothing. If a "real" signal event was seen recently,
        //    keep the envelope pinned so the sub doesn't cut out on a brief
        //    string-noise gap.
        if (envState > 0.01f)
            holdCounter = holdSamples;
        else if (holdCounter > 0)
        {
            --holdCounter;
            envState = std::max(envState, 0.005f);
        }

        const float subWithEnv = subSquare * envState;

        // 7. Per-channel output stage: pitch shifter (transposes the sub by
        //    the user-set semitones), then tone-controlled LPF, DC blocker,
        //    and equal-power crossfade with the dry snapshot.
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
            auto& outLpf = octaveOutputLpfState[channelIndex];
            auto& dcX1   = octaveDcBlockX1[channelIndex];
            auto& dcY1   = octaveDcBlockY1[channelIndex];

            // Always run the pitch shifter so the path is identical at
            // every transpose setting — that gives us constant group
            // delay and no click when the user crosses through 0. The
            // shifter's natural latency is grainLength / pitchRatio
            // (1024..4096 samples across the +/-12 st range); we pad
            // the rest with a fractional delay line so the total is
            // always the worst-case 4096 samples. The host reports this
            // fixed value once in prepareToPlay.
            const auto pitchShifted = octavePitchShifters[channelIndex]
                                          .processSample(subWithEnv, pitchRatio);

            // Push the shifter output and read it back at a delay that
            // makes the total latency (shifter + pad) = worstCaseLookback.
            // DelayLine::read() does linear interpolation, so the
            // fractional pad amount (e.g. 1365.33 at certain transpose
            // settings) is read with sub-sample accuracy -- the read
            // position changes smoothly as the knob moves, with no
            // integer-quantization zipper.
            auto& pad = octavePadDelay[channelIndex];
            pad.push(pitchShifted);
            const auto padded = pad.read(octaveShifterPadSamples);

            outLpf += outputLpfCoeff * (padded - outLpf) + 1.0e-25f;

            // Standard one-pole DC blocker: y[n] = x[n] - x[n-1] + R*y[n-1].
            // (The previous form scaled the differencing term by R, which
            // is wrong and gave a frequency-dependent leak.)
            const auto dcWet = outLpf - dcX1 + dcBlockCoeff * dcY1;
            dcX1 = outLpf;
            dcY1 = dcWet;

            const auto wet  = dcWet * levelGain;
            const auto dry  = dryScratchBuffer.getSample(channel, sample);
            buffer.setSample(channel, sample, dry * dryGain + wet * wetGain);
        }
    }
}


void SimpleEQAudioProcessor::applyDoubler(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.doublerBypassed)
        return;

    const auto mix = juce::jlimit(0.0f, 1.0f, chainSettings.doublerMix);
    if (mix <= 0.0f)
        return;

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const auto sampleRate = static_cast<float>(juce::jmax(1.0, getSampleRate()));
    const auto baseDelaySamples = juce::jlimit(0.0f, static_cast<float>(DelayLine::size - 2),
                                               chainSettings.doublerDelayMs * sampleRate / 1000.0f);
    const auto detuneRatio = std::pow(2.0f, chainSettings.doublerDetuneCents / 1200.0f);

    // Classic Haas doubler. The L channel is read, pushed through a short delay,
    // and pitch-shifted to make one "doubled voice". That voice is added to the
    // R channel; L passes through dry. For a mono buffer the voice is added to
    // the only channel so the effect is still audible.
    auto& delayLine = doublerDelayLines[0];
    auto& shifter = doublerPitchShifters[0];

    const auto* leftRead = buffer.getReadPointer(0);
    auto* leftWrite = buffer.getWritePointer(0);
    auto* rightWrite = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto source = leftRead[sample];
        delayLine.push(source);
        const auto delayed = delayLine.read(baseDelaySamples);
        const auto doubled = shifter.processSample(delayed, detuneRatio);

        if (rightWrite != nullptr)
            rightWrite[sample] += doubled * mix;
        else
            leftWrite[sample] += doubled * mix;
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


void SimpleEQAudioProcessor::applyOverdrive(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.overdriveBypassed)
        return;

    const auto driveDb = chainSettings.overdriveDriveInDecibels;
    const auto driveNorm = juce::jlimit(0.0f, 1.0f, driveDb / 24.0f);

    // Drive pushes the input well past unity into the clipper. The fixed
    // bias term keeps the clipper in its asymmetric range even at low
    // drive settings so the TS character is audible from the get-go.
    const auto preGain = juce::Decibels::decibelsToGain(2.0f + driveDb * 1.1f);
    const auto wetMix = juce::jmap(driveNorm, 0.0f, 1.0f, 0.20f, 0.90f);
    const auto autoGain = juce::jlimit(0.40f, 1.0f, std::pow(preGain, -0.32f));
    const auto levelGain = juce::Decibels::decibelsToGain(chainSettings.overdriveLevelInDecibels);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dryScratchBuffer.copyFrom (ch, 0, buffer.getReadPointer (ch), buffer.getNumSamples());

    // Pre-clip HPF tightens the bass response before the soft clipper.
    const auto hostSampleRate = juce::jmax(1.0, getSampleRate());
    const auto preHpfCoeff = std::exp(-2.0f * juce::MathConstants<float>::pi * 150.0f
                                      / static_cast<float>(hostSampleRate));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = buffer.getWritePointer(channel);
        auto& x1 = overdrivePreHpfX1[channelIndex];
        auto& y1 = overdrivePreHpfY1[channelIndex];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto x = channelData[sample];
            const auto y = preHpfCoeff * (y1 + x - x1);
            x1 = x;
            y1 = y;
            channelData[sample] = y;
        }
    }

    // Upsample so the asymmetric soft clipper runs at 2x.
    juce::dsp::AudioBlock<float> ioBlock(buffer);
    auto osBlock = overdriveOversampler.processSamplesUp(ioBlock);

    const auto osSampleRate = 2.0 * hostSampleRate;
    const auto toneNorm = juce::jlimit(0.0f, 1.0f, chainSettings.overdriveTone);
    // TS-style tone is a high-cut: lower tone = darker, higher tone = more open.
    const auto cutoffHz = juce::jmap(toneNorm, 0.0f, 1.0f, 700.0f, 5500.0f);
    const auto smoothing = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz
                                           / static_cast<float>(osSampleRate));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* channelData = osBlock.getChannelPointer(channel);
        auto& toneState = overdriveToneState[static_cast<size_t>(juce::jmin(channel, 1))];

        for (int sample = 0; sample < osBlock.getNumSamples(); ++sample)
        {
            const auto driven = channelData[sample] * preGain;

            // Tube-Screamer-style asymmetric soft clip. The positive half
            // is driven harder (more compression, more "honk") while the
            // negative half keeps more headroom. The two different gain
            // scales generate even-order harmonics, which is what gives
            // the TS its valve-like character.
            const auto ax = std::abs(driven);
            const auto yPos = (1.8f * driven) / (1.0f + 1.8f * ax) * 0.55f;
            const auto yNeg = (0.85f * driven) / (1.0f + 0.85f * ax) * 0.75f;
            auto wet = (driven >= 0.0f ? yPos : yNeg) * autoGain;

            toneState += smoothing * (wet - toneState);
            channelData[sample] = toneState * levelGain;
        }
    }

    overdriveOversampler.processSamplesDown(ioBlock);

    // DC blocker. The asymmetric clipper is the main DC source in this
    // stage, so a blocker at the output keeps the baseline clean.
    constexpr float dcBlockCoeff = 0.995f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = buffer.getWritePointer(channel);
        auto& x1 = overdriveDcBlockX1[channelIndex];
        auto& y1 = overdriveDcBlockY1[channelIndex];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto x = channelData[sample];
            const auto y = dcBlockCoeff * (y1 + x - x1);
            x1 = x;
            y1 = y;
            channelData[sample] = y;
        }
    }

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* wetData = buffer.getWritePointer(channel);
        const auto* dryData = dryScratchBuffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            wetData[sample] = juce::jlimit(-1.0f, 1.0f, juce::jmap(wetMix, dryData[sample], wetData[sample]));
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

    auto softClip = [](float x)
    {
        const auto ax = std::abs(x);
        if (ax <= 1.0f)
            return x - (x * x * x) / 3.0f;

        return (x > 0.0f ? 2.0f / 3.0f : -2.0f / 3.0f);
    };

    // Snapshot the dry signal into a pre-allocated scratch buffer so the
    // final crossfade can read the unmodified input. Captured *before*
    // the pre-HPF so the crossfade keeps the full low end in the
    // unprocessed path.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dryScratchBuffer.copyFrom (ch, 0, buffer.getReadPointer (ch), buffer.getNumSamples());

    // Pre-distortion high-pass. Low-frequency energy drives waveshapers
    // into clipping first and produces a loose, muddy texture. Filtering
    // out the bottom ~150 Hz before the shaper keeps the distortion tight
    // and articulate.
    const auto hostSampleRate = juce::jmax(1.0, getSampleRate());
    const auto preHpfCoeff = std::exp(-2.0f * juce::MathConstants<float>::pi * 150.0f
                                      / static_cast<float>(hostSampleRate));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = buffer.getWritePointer(channel);
        auto& x1 = distortionPreHpfX1[channelIndex];
        auto& y1 = distortionPreHpfY1[channelIndex];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto x = channelData[sample];
            const auto y = preHpfCoeff * (y1 + x - x1);
            x1 = x;
            y1 = y;
            channelData[sample] = y;
        }
    }

    // Upsample so the non-linear tanh + cubic shaping runs at 2x. The
    // oversampling filter's stopband then strips the harmonics that would
    // otherwise alias back into the audible band on the way back down.
    juce::dsp::AudioBlock<float> ioBlock(buffer);
    auto osBlock = distOversampler.processSamplesUp(ioBlock);

    const auto osSampleRate = 2.0 * juce::jmax(1.0, getSampleRate());
    const auto toneNorm = juce::jlimit(0.0f, 1.0f, chainSettings.distortionTone);
    const auto cutoffHz = juce::jmap(toneNorm, 0.0f, 1.0f, 1000.0f, 10000.0f);
    const auto smoothing = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz
                                           / static_cast<float>(osSampleRate));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* channelData = osBlock.getChannelPointer(channel);
        auto& toneState = distortionToneState[static_cast<size_t>(juce::jmin(channel, 1))];

        for (int sample = 0; sample < osBlock.getNumSamples(); ++sample)
        {
            const auto driven = channelData[sample] * preGain;

            // Fast tanh approximation: x / (1 + |x|). The blend with the
            // cubic clipper masks the slight shape difference but cuts
            // the per-sample cost of std::tanh dramatically.
            const auto tanhShape = driven / (1.0f + std::abs(driven));
            const auto cubicShape = softClip(driven * 0.75f) * 1.4f;
            auto wet = (0.78f * tanhShape + 0.22f * cubicShape) * autoGain;

            toneState += smoothing * (wet - toneState);
            channelData[sample] = toneState * levelGain;
        }
    }

    distOversampler.processSamplesDown(ioBlock);

    // DC blocker. Asymmetric shaping (and even the tanh approx slightly)
    // can inject DC offset, which eats headroom and pops on transport
    // start/stop. A 1-pole HPF at ~38 Hz at 48 kHz pulls the baseline
    // back to 0 without touching any audio band.
    constexpr float dcBlockCoeff = 0.995f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = buffer.getWritePointer(channel);
        auto& x1 = distortionDcBlockX1[channelIndex];
        auto& y1 = distortionDcBlockY1[channelIndex];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto x = channelData[sample];
            const auto y = dcBlockCoeff * (y1 + x - x1);
            x1 = x;
            y1 = y;
            channelData[sample] = y;
        }
    }

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* wetData = buffer.getWritePointer(channel);
        const auto* dryData = dryScratchBuffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            wetData[sample] = juce::jlimit(-1.0f, 1.0f, juce::jmap(wetMix, dryData[sample], wetData[sample]));
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

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dryScratchBuffer.copyFrom (ch, 0, buffer.getReadPointer (ch), buffer.getNumSamples());

    // Pre-clipping high-pass. Fuzz is supposed to sag under heavy low
    // strings, so we only roll off the deepest sub-rumble (around 50 Hz)
    // to keep the DC blocker downstream happy. Anything above that is
    // allowed to crush the waveshaper for that classic velcro / wall of
    // sound feel.
    const auto hostSampleRate = juce::jmax(1.0, getSampleRate());
    const auto preHpfCoeff = std::exp(-2.0f * juce::MathConstants<float>::pi * 50.0f
                                      / static_cast<float>(hostSampleRate));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = buffer.getWritePointer(channel);
        auto& x1 = fuzzPreHpfX1[channelIndex];
        auto& y1 = fuzzPreHpfY1[channelIndex];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto x = channelData[sample];
            const auto y = preHpfCoeff * (y1 + x - x1);
            x1 = x;
            y1 = y;
            channelData[sample] = y;
        }
    }

    // Hard asymmetric clipping generates even stronger harmonics than the
    // distortion, so it benefits even more from 2x oversampling.
    juce::dsp::AudioBlock<float> ioBlock(buffer);
    auto osBlock = fuzzOversampler.processSamplesUp(ioBlock);

    const auto osSampleRate = 2.0 * juce::jmax(1.0, getSampleRate());
    const auto toneNorm = juce::jlimit(0.0f, 1.0f, chainSettings.fuzzTone);
    const auto cutoffHz = juce::jmap(toneNorm, 0.0f, 1.0f, 1000.0f, 10000.0f);
    const auto smoothing = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz
                                           / static_cast<float>(osSampleRate));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = osBlock.getChannelPointer(channel);
        auto& toneState = fuzzToneState[channelIndex];
        auto& biasState = fuzzBiasFollower[channelIndex];

        for (int sample = 0; sample < osBlock.getNumSamples(); ++sample)
        {
            const auto driven = channelData[sample] * preGain;

            // One-pole follower tracks recent signal level. Loud passages
            // drag the bias down, emulating the starving-transistor sag of
            // vintage fuzz circuits and giving us a "spitting" texture on
            // hard strums.
            const auto inputAbs = std::abs(driven);
            biasState += 0.001f * (inputAbs - biasState);

            const float dynamicPos = juce::jmax(0.15f, 0.6f - (biasState * 0.2f));
            const float dynamicNeg = juce::jmax(0.10f, 0.4f - (biasState * 0.25f));

            const auto asymmetric = driven >= 0.0f
                                        ? std::min(driven, dynamicPos)
                                        : std::max(driven, -dynamicNeg);

            // Smooth absolute-value approximation. std::abs creates a sharp
            // corner at the zero crossing that excites ultra-high digital
            // harmonics. The sqrt(x^2 + eps) form rounds that corner,
            // making the octave-up layer sound like a warm analog
            // transformer/diode pair.
            const auto rectified = std::sqrt((asymmetric * asymmetric) + 0.005f) * 0.6f;

            auto wet = ((1.0f - octaveBlend) * asymmetric + octaveBlend * rectified) * autoGain;

            toneState += smoothing * (wet - toneState);
            channelData[sample] = toneState * levelGain;
        }
    }

    fuzzOversampler.processSamplesDown(ioBlock);

    // DC blocker. Asymmetric fuzz clipping is the worst offender for
    // DC offset in this chain, so the blocker is especially important.
    constexpr float dcBlockCoeff = 0.995f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto channelIndex = static_cast<size_t>(juce::jmin(channel, 1));
        auto* channelData = buffer.getWritePointer(channel);
        auto& x1 = fuzzDcBlockX1[channelIndex];
        auto& y1 = fuzzDcBlockY1[channelIndex];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto x = channelData[sample];
            const auto y = dcBlockCoeff * (y1 + x - x1);
            x1 = x;
            y1 = y;
            channelData[sample] = y;
        }
    }

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* wetData = buffer.getWritePointer(channel);
        const auto* dryData = dryScratchBuffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            wetData[sample] = juce::jlimit(-1.0f, 1.0f, juce::jmap(wetMix, dryData[sample], wetData[sample]));
    }
}

void SimpleEQAudioProcessor::applySynthFuzz(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.synthFuzzBypassed)
        return;

    const auto mix = juce::jlimit(0.0f, 1.0f, chainSettings.synthFuzzMix);
    if (mix <= 0.0f)
        return;

    const auto sampleRate = static_cast<float>(juce::jmax(1.0, getSampleRate()));
    const auto baseDelaySamples = juce::jlimit(0.0f, static_cast<float>(DelayLine::size - 2),
                                               chainSettings.synthFuzzDelayMs * sampleRate / 1000.0f);
    const auto detuneRatio = std::pow(2.0f, chainSettings.synthFuzzDetuneCents / 1200.0f);

    // --- Fuzz stage coefficients ---------------------------------------
    constexpr float preEmphasisHz = 300.0f;
    const auto preEmphasisCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi
                                                  * preEmphasisHz / sampleRate);
    
    // OPTIMIZATION: Calculate the inverse tanh of the drive out here so the 
    // CPU doesn't have to compute std::tanh(drive) thousands of times per block.
    const auto drive = juce::jlimit(1.0f, 20.0f, chainSettings.synthFuzzDrive);
    const auto invTanhDrive = 1.0f / std::tanh(drive);

    // Equal-power dry/wet
    const auto mixAngle = mix * juce::MathConstants<float>::halfPi;
    const auto dryGain  = std::cos(mixAngle);
    const auto wetGain  = std::sin(mixAngle);

    const auto levelGain = juce::Decibels::decibelsToGain(chainSettings.synthFuzzLevelInDecibels);

    auto& delayLine = synthFuzzDelayLines[0];
    auto& shifter   = synthFuzzPitchShifters[0];
    auto& hpfState  = synthFuzzHpfState[0]; 

    // STEREO FIX: Fetch separate read pointers for left and right channels
    const auto* leftRead  = buffer.getReadPointer(0);
    const auto* rightRead = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : nullptr;
    
    auto* leftWrite  = buffer.getWritePointer(0);
    auto* rightWrite = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        // Pull left and right dry signals independently
        const auto dryLeft  = leftRead[sample];
        const auto dryRight = (rightRead != nullptr) ? rightRead[sample] : dryLeft;

        // High-pass pre-emphasis filter tracking the primary guitar signal (Left/Mono input)
        hpfState += preEmphasisCoeff * (dryLeft - hpfState);
        const auto highs = dryLeft - hpfState;
        
        // Optimized waveshaper loop calculation
        const auto fuzzed = std::tanh(highs * drive) * invTanhDrive;

        // Pseudo-Stereo Doubler
        delayLine.push(fuzzed);
        const auto delayed = delayLine.read(baseDelaySamples);
        const auto doubled = shifter.processSample(delayed, detuneRatio);

        if (rightWrite != nullptr)
        {
            // STEREO FIX: dryLeft goes to Left out, dryRight goes to Right out.
            // This stops the pedal from collapsing preceding stereo effects to mono.
            leftWrite[sample]  = levelGain * (dryLeft * dryGain + fuzzed * wetGain);
            rightWrite[sample] = levelGain * (dryRight * dryGain + doubled * wetGain);
        }
        else
        {
            // Mono fallback
            leftWrite[sample] = levelGain * (dryLeft * dryGain + 0.5f * (fuzzed + doubled) * wetGain);
        }
    }
}

void SimpleEQAudioProcessor::applyTremolo(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings)
{
    if (chainSettings.tremoloBypassed)
        return;

    const auto speedHz = juce::jlimit(0.05f, 20.0f, chainSettings.tremoloSpeedHz);
    const auto depth   = juce::jlimit(0.0f, 1.0f, chainSettings.tremoloDepth);
    if (depth <= 0.0f || speedHz <= 0.0f)
        return;

    const auto sampleRate = static_cast<float>(juce::jmax(1.0, getSampleRate()));
    const auto phaseInc   = static_cast<float>(speedHz / sampleRate);
    const auto lfoIndex   = juce::jlimit(0, 2, chainSettings.tremoloLfoIndex);

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    // 0.5 = R channel 180 degrees out of phase with L (true stereo panning).
    // 0.0 = both channels modulate identically (in-phase, classic mono tremolo).
    const auto phaseOffset = chainSettings.tremoloStereoPhase ? 0.5f : 0.0f;

    // Outer loop runs through channels for optimized CPU cache locality
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        
        // Establish starting phase for this block execution
        float phase = tremoloLfoPhase;
        
        // If this is the right channel, shift its phase to create the stereo widening/panning effect
        if (channel == 1)
        {
            phase += phaseOffset;
            
            // Keep the shifted phase bounded cleanly between 0.0 and 1.0
            if (phase >= 1.0f) phase -= 1.0f;
            if (phase < 0.0f)  phase += 1.0f;
        }

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float lfo;
            
            if (lfoIndex == 1) // Triangle
            {
                lfo = (phase < 0.5f) ? (phase * 2.0f) : ((1.0f - phase) * 2.0f);
            }
            else if (lfoIndex == 2) // Analog-Slewed Square (Anti-Click Fix)
            {
                // Saturating a sine wave with tanh creates a hard-chopping shape 
                // but rounds off the sharp corners, removing the transients that cause clicks.
                const auto sineWave = std::sin(phase * juce::MathConstants<float>::twoPi);
                lfo = 0.5f + 0.5f * std::tanh(sineWave * 6.0f); 
            }
            else // Sine (Default)
            {
                lfo = 0.5f + 0.5f * std::sin(phase * juce::MathConstants<float>::twoPi);
            }

            // Apply unipolar depth attenuation
            const auto gain = 1.0f - depth * (1.0f - lfo);
            channelData[sample] *= gain;

            // Advance the phase for the next sample iteration
            phase += phaseInc;
            if (phase >= 1.0f)
                phase -= 1.0f;
        }
    }

    // Instead of saving the channel-loop's mutated local phase, we mathematically advance 
    // the master block accumulator phase based exactly on the elapsed sample time.
    tremoloLfoPhase = std::fmod(tremoloLfoPhase + (static_cast<float>(numSamples) * phaseInc), 1.0f);
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

    // The min/max here MUST match kOctaveTranspose{Min,Max}Semitones in
    // PluginProcessor.h -- if you change one, change both. They are
    // duplicated (rather than referenced) because juce::NormalisableRange
    // wants float literals at the point of construction, not a
    // constexpr reference. The header constants drive the latency
    // report and the runtime clamp in applyOctave.
    layout.add(std::make_unique<juce::AudioParameterFloat>("Octave Transpose",
                                                           "Octave Transpose",
                                                           juce::NormalisableRange<float>(kOctaveTransposeMinSemitones,
                                                                                          kOctaveTransposeMaxSemitones,
                                                                                          1.f, 1.f),
                                                           0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Octave Mix",
                                                           "Octave Mix",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f, 1.0f),
                                                           0.5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Octave Level",
                                                           "Octave Level",
                                                           juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f, 1.0f),
                                                           0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Octave Tone",
                                                           "Octave Tone",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f, 1.0f),
                                                           0.5f));

    layout.add(std::make_unique<juce::AudioParameterBool>("Octave Mono Detector",
                                                         "Octave Mono Detector",
                                                         true));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Doubler Mix",
                                                           "Doubler Mix",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.6f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Doubler Delay",
                                                           "Doubler Delay",
                                                           juce::NormalisableRange<float>(0.f, 100.f, 0.5f, 1.f),
                                                           18.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Doubler Detune",
                                                           "Doubler Detune",
                                                           juce::NormalisableRange<float>(-50.f, 50.f, 1.f, 1.f),
                                                           6.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Synth Fuzz Mix",
                                                           "Synth Fuzz Mix",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Synth Fuzz Delay",
                                                           "Synth Fuzz Delay",
                                                           juce::NormalisableRange<float>(0.f, 100.f, 0.5f, 1.f),
                                                           18.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Synth Fuzz Detune",
                                                           "Synth Fuzz Detune",
                                                           juce::NormalisableRange<float>(-50.f, 50.f, 1.f, 1.f),
                                                           6.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Synth Fuzz Drive",
                                                           "Synth Fuzz Drive",
                                                           juce::NormalisableRange<float>(1.f, 20.f, 0.05f, 1.f),
                                                           1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Synth Fuzz Level",
                                                           "Synth Fuzz Level",
                                                           juce::NormalisableRange<float>(-24.f, 12.f, 0.1f, 1.f),
                                                           0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Tremolo Speed",
                                                           "Tremolo Speed",
                                                           juce::NormalisableRange<float>(0.1f, 15.f, 0.05f, 1.f),
                                                           5.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Tremolo Depth",
                                                           "Tremolo Depth",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.5f));

    layout.add(std::make_unique<juce::AudioParameterChoice>("Tremolo LFO",
                                                            "Tremolo LFO",
                                                            juce::StringArray { "Sine", "Triangle", "Square" },
                                                            0));

    layout.add(std::make_unique<juce::AudioParameterBool>("Tremolo Stereo Phase",
                                                          "Tremolo Stereo Phase",
                                                          true));

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

    layout.add(std::make_unique<juce::AudioParameterFloat>("Overdrive Drive",
                                                           "Overdrive Drive",
                                                           juce::NormalisableRange<float>(0.f, 24.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Overdrive Tone",
                                                           "Overdrive Tone",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Overdrive Level",
                                                           "Overdrive Level",
                                                           juce::NormalisableRange<float>(-24.f, 12.f, 0.1f, 1.f),
                                                           0.f));

    layout.add(std::make_unique<juce::AudioParameterBool>("Overdrive Bypassed",
                                                          "Overdrive Bypassed",
                                                          false));

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
    layout.add(std::make_unique<juce::AudioParameterBool>("Synth Fuzz Bypassed", "Synth Fuzz Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Tremolo Bypassed", "Tremolo Bypassed", false));
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
