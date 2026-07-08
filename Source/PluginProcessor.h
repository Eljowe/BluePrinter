/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>

#include <array>
template<typename T>
struct Fifo
{
    void prepare(int numChannels, int numSamples)
    {
        static_assert( std::is_same_v<T, juce::AudioBuffer<float>>,
                      "prepare(numChannels, numSamples) should only be used when the Fifo is holding juce::AudioBuffer<float>");
        for( auto& buffer : buffers)
        {
            buffer.setSize(numChannels,
                           numSamples,
                           false,   //clear everything?
                           true,    //including the extra space?
                           true);   //avoid reallocating if you can?
            buffer.clear();
        }
    }
    
    void prepare(size_t numElements)
    {
        static_assert( std::is_same_v<T, std::vector<float>>,
                      "prepare(numElements) should only be used when the Fifo is holding std::vector<float>");
        for( auto& buffer : buffers )
        {
            buffer.clear();
            buffer.resize(numElements, 0);
        }
    }
    
    bool push(const T& t)
    {
        auto write = fifo.write(1);
        if( write.blockSize1 > 0 )
        {
            buffers[write.startIndex1] = t;
            return true;
        }
        
        return false;
    }
    
    bool pull(T& t)
    {
        auto read = fifo.read(1);
        if( read.blockSize1 > 0 )
        {
            t = buffers[read.startIndex1];
            return true;
        }
        
        return false;
    }
    
    int getNumAvailableForReading() const
    {
        return fifo.getNumReady();
    }
private:
    static constexpr int Capacity = 30;
    std::array<T, Capacity> buffers;
    juce::AbstractFifo fifo {Capacity};
};

enum Channel
{
    Right, //effectively 0
    Left //effectively 1
};

template<typename BlockType>
struct SingleChannelSampleFifo
{
    SingleChannelSampleFifo(Channel ch) : channelToUse(ch)
    {
        prepared.set(false);
    }
    
    void update(const BlockType& buffer)
    {
        jassert(prepared.get());
        jassert(buffer.getNumChannels() > channelToUse );
        auto* channelPtr = buffer.getReadPointer(channelToUse);
        
        for( int i = 0; i < buffer.getNumSamples(); ++i )
        {
            pushNextSampleIntoFifo(channelPtr[i]);
        }
    }

    void prepare(int bufferSize)
    {
        prepared.set(false);
        size.set(bufferSize);
        
        bufferToFill.setSize(1,             //channel
                             bufferSize,    //num samples
                             false,         //keepExistingContent
                             true,          //clear extra space
                             true);         //avoid reallocating
        audioBufferFifo.prepare(1, bufferSize);
        fifoIndex = 0;
        prepared.set(true);
    }
    //==============================================================================
    int getNumCompleteBuffersAvailable() const { return audioBufferFifo.getNumAvailableForReading(); }
    bool isPrepared() const { return prepared.get(); }
    int getSize() const { return size.get(); }
    //==============================================================================
    bool getAudioBuffer(BlockType& buf) { return audioBufferFifo.pull(buf); }
private:
    Channel channelToUse;
    int fifoIndex = 0;
    Fifo<BlockType> audioBufferFifo;
    BlockType bufferToFill;
    juce::Atomic<bool> prepared = false;
    juce::Atomic<int> size = 0;
    
    void pushNextSampleIntoFifo(float sample)
    {
        if (fifoIndex == bufferToFill.getNumSamples())
        {
            auto ok = audioBufferFifo.push(bufferToFill);

            juce::ignoreUnused(ok);
            
            fifoIndex = 0;
        }
        
        bufferToFill.setSample(0, fifoIndex, sample);
        ++fifoIndex;
    }
};

enum Slope
{
    Slope_12,
    Slope_24,
    Slope_36,
    Slope_48
};

//==============================================================================
// Two-voice, time-domain granular pitch shifter with Hann-windowed overlap-add.
// Voices are phase-staggered by half a grain; on each phase wrap the read
// pointer jumps back so the voice always reads the most recent grainLength /
// pitchRatio input samples. Sum of two half-offset Hann windows is 1, so
// COLA holds and amplitude is preserved.
class GranularPitchShifter
{
public:
    static constexpr int bufferSize = 4096;
    static constexpr int grainLength = 1024;

    void prepare()
    {
        std::fill(std::begin(buffer), std::end(buffer), 0.0f);
        writePos = 0;
        for (int v = 0; v < 2; ++v)
        {
            voices[v].readPos = 0.0f;
            voices[v].phase = (v == 0) ? 0.0f : static_cast<float>(grainLength) * 0.5f;
            lpfState[v] = 0.0f;
        }
    }

    void setSampleRate (double sampleRate)
    {
        // One-pole low-pass to take the edge off aliasing when shifting up.
        const auto cutoffHz = juce::jmin (8000.0f, static_cast<float>(sampleRate) * 0.4f);
        lpfCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * cutoffHz
                                    / static_cast<float>(sampleRate));
    }

    float processSample (float input, float pitchRatio)
    {
        if (pitchRatio < 0.25f) pitchRatio = 0.25f;
        if (pitchRatio > 4.0f)  pitchRatio = 4.0f;

        buffer[writePos] = input;

        float output = 0.0f;
        for (int v = 0; v < 2; ++v)
        {
            const auto readIdx = static_cast<int>(voices[v].readPos) & (bufferSize - 1);
            output += buffer[readIdx] * hannWindow (voices[v].phase);

            voices[v].readPos += pitchRatio;
            voices[v].phase   += pitchRatio;

            if (voices[v].phase >= static_cast<float>(grainLength))
            {
                voices[v].phase -= static_cast<float>(grainLength);
                const auto lookback = static_cast<float>(grainLength) / pitchRatio;
                auto newRead = static_cast<float>(writePos) - lookback;
                newRead = std::fmod (newRead, static_cast<float>(bufferSize));
                if (newRead < 0.0f)
                    newRead += static_cast<float>(bufferSize);
                voices[v].readPos = newRead;
            }
        }

        // Soft LP to dampen upward-shift aliasing.
        lpfState[0] += lpfCoeff * (output - lpfState[0]);
        lpfState[1] += lpfCoeff * (lpfState[0] - lpfState[1]);
        output = lpfState[1];

        writePos = (writePos + 1) & (bufferSize - 1);
        return output;
    }

private:
    static float hannWindow (float phase)
    {
        if (phase < 0.0f || phase >= static_cast<float>(grainLength))
            return 0.0f;
        return 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                        * phase / static_cast<float>(grainLength)));
    }

    std::array<float, bufferSize> buffer {};
    int writePos = 0;
    struct Voice { float readPos = 0.0f; float phase = 0.0f; };
    Voice voices[2];

    float lpfCoeff = 0.5f;
    float lpfState[2] { 0.0f, 0.0f };
};

//==============================================================================
// Simple fractional delay line. Buffer size must be a power of two.
class DelayLine
{
public:
    static constexpr int size = 262144;

    void prepare()
    {
        std::fill(std::begin(buffer), std::end(buffer), 0.0f);
        writePos = 0;
    }

    void push (float sample)
    {
        buffer[writePos] = sample;
        writePos = (writePos + 1) & (size - 1);
    }

    int getWritePos() const { return writePos; }

    float read (float delaySamples) const
    {
        const auto clamped = juce::jlimit (0.0f, static_cast<float>(size - 1), delaySamples);
        const auto readPos = static_cast<float>(writePos) - clamped - 1.0f;
        auto wrapped = std::fmod (readPos, static_cast<float>(size));
        if (wrapped < 0.0f)
            wrapped += static_cast<float>(size);
        const auto idx0 = static_cast<int>(wrapped) & (size - 1);
        const auto idx1 = (idx0 + 1) & (size - 1);
        const auto frac = wrapped - std::floor (wrapped);
        return buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;
    }

private:
    std::array<float, size> buffer {};
    int writePos = 0;
};

struct ChainSettings
{
    float peakFreq { 0 }, peakGainInDecibels{ 0 }, peakQuality {1.f};
    float inputGainInDecibels { 0.f };
    float tunerReferencePitchHz { 440.f };
    float gateThresholdInDecibels { -60.f };
    float compressorAmount { 0.f };
    float compressorTone { 0.f };
    float compressorLevelInDecibels { 0.f };
    float octaveTransposeSemitones { 0.0f };
    float doublerMix { 0.0f };
    float doublerDelayMs { 20.0f };
    float doublerDetuneCents { 5.0f };
    float delayMix { 0.35f };
    float delayTimeLMs { 350.0f };
    float delayTimeRMs { 350.0f };
    float delayFeedback { 0.35f };
    bool  delayModeIsDual { false };
    float outputGainInDecibels { 0.f };
    float distortionDriveInDecibels { 0.f };
    float distortionTone { 0.7f };
    float distortionLevelInDecibels { 0.f };
    float fuzzDriveInDecibels { 0.f };
    float fuzzTone { 0.7f };
    float fuzzLevelInDecibels { 0.f };
    float lowCutFreq { 0 }, highCutFreq { 0 };

    float reverbSize { 0.5f };
    float reverbDamping { 0.5f };
    float reverbMix { 0.0f };
    float reverbWidth { 1.0f };

    Slope lowCutSlope { Slope::Slope_12 }, highCutSlope { Slope::Slope_12 };

    bool lowCutBypassed { false }, peakBypassed { false }, highCutBypassed { false };
    bool distortionBypassed { false }, compressorBypassed { false };
    bool fuzzBypassed { false };
    bool reverbBypassed { false };
    bool tunerBypassed { false };
    bool gateBypassed { false };
    bool octaveBypassed { false };
    bool doublerBypassed { false };
    bool delayBypassed { false };
};

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts);

using Filter = juce::dsp::IIR::Filter<float>;

using CutFilter = juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter>;

using MonoChain = juce::dsp::ProcessorChain<CutFilter, Filter, CutFilter>;

enum ChainPositions
{
    LowCut,
    Peak,
    HighCut
};

using Coefficients = Filter::CoefficientsPtr;
void updateCoefficients(Coefficients& old, const Coefficients& replacements);

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate);

template<int Index, typename ChainType, typename CoefficientType>
void update(ChainType& chain, const CoefficientType& coefficients)
{
    updateCoefficients(chain.template get<Index>().coefficients, coefficients[Index]);
    chain.template setBypassed<Index>(false);
}

template<typename ChainType, typename CoefficientType>
void updateCutFilter(ChainType& chain,
                     const CoefficientType& coefficients,
                     const Slope& slope)
{
    chain.template setBypassed<0>(true);
    chain.template setBypassed<1>(true);
    chain.template setBypassed<2>(true);
    chain.template setBypassed<3>(true);
    
    switch( slope )
    {
        case Slope_48:
        {
            update<3>(chain, coefficients);
        }
        case Slope_36:
        {
            update<2>(chain, coefficients);
        }
        case Slope_24:
        {
            update<1>(chain, coefficients);
        }
        case Slope_12:
        {
            update<0>(chain, coefficients);
        }
    }
}

inline auto makeLowCutFilter(const ChainSettings& chainSettings, double sampleRate )
{
    return juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(chainSettings.lowCutFreq,
                                                                                       sampleRate,
                                                                                       2 * (chainSettings.lowCutSlope + 1));
}

inline auto makeHighCutFilter(const ChainSettings& chainSettings, double sampleRate )
{
    return juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(chainSettings.highCutFreq,
                                                                                      sampleRate,
                                                                                      2 * (chainSettings.highCutSlope + 1));
}
//==============================================================================
/**
*/
class SimpleEQAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    SimpleEQAudioProcessor();
    ~SimpleEQAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts {*this, nullptr, "Parameters", createParameterLayout()};

    bool consumeInputClippingFlag()
    {
        return inputClippingDetected.exchange(false, std::memory_order_acq_rel);
    }

    bool consumeOutputClippingFlag()
    {
        return outputClippingDetected.exchange(false, std::memory_order_acq_rel);
    }

    float consumeInputPeakLevel()
    {
        return inputPeakLevel.exchange(0.0f, std::memory_order_acq_rel);
    }

    float consumeOutputPeakLevel()
    {
        return outputPeakLevel.exchange(0.0f, std::memory_order_acq_rel);
    }

    float getTunerDetectedFrequencyHz() const
    {
        return tunerDetectedFrequencyHz.load(std::memory_order_acquire);
    }

    float getTunerDetectedCents() const
    {
        return tunerDetectedCents.load(std::memory_order_acquire);
    }

    int getTunerDetectedNoteIndex() const
    {
        return tunerDetectedNoteIndex.load(std::memory_order_acquire);
    }

    float getTunerDetectedLevelDb() const
    {
        return tunerDetectedLevel.load(std::memory_order_acquire);
    }
    
    using BlockType = juce::AudioBuffer<float>;
    SingleChannelSampleFifo<BlockType> leftChannelFifo { Channel::Left };
    SingleChannelSampleFifo<BlockType> rightChannelFifo { Channel::Right };
private:
    MonoChain leftChain, rightChain;
    
    void updatePeakFilter(const ChainSettings& chainSettings);
    void applyCompressor(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings);
    void applyOctave(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings);
    void applyDoubler(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings);
    void applyDelay(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings);
    void applyDistortion(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings);
    void applyFuzz(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings);
    void applyReverb(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings);
    void applyGain(juce::AudioBuffer<float>& buffer, float gainDecibels);
    void applyTuner(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings);
    void applyGate(juce::AudioBuffer<float>& buffer, const ChainSettings& chainSettings);
    void pushPeakLevel(std::atomic<float>& targetPeak, float peakValue);

    
    
    
    void updateLowCutFilters(const ChainSettings& chainSettings);
    void updateHighCutFilters(const ChainSettings& chainSettings);
    
    void updateFilters();

    std::atomic<bool> inputClippingDetected { false };
    std::atomic<bool> outputClippingDetected { false };
    std::atomic<float> inputPeakLevel { 0.0f };
    std::atomic<float> outputPeakLevel { 0.0f };

    std::atomic<float> tunerDetectedFrequencyHz { 0.0f };
    std::atomic<float> tunerDetectedCents { 0.0f };
    std::atomic<float> tunerDetectedLevel { 0.0f };
    std::atomic<int>   tunerDetectedNoteIndex { -1 };

    std::vector<float> tunerYinBuffer;
    std::vector<float> tunerAnalysisBuffer;
    int tunerYinBufferWriteIndex { 0 };
    int tunerSamplesUntilNextAnalysis { 0 };
    double tunerAnalysisSampleRate { 48000.0 };

    std::array<float, 2> compressorGainReductionDb { 0.0f, 0.0f };
    std::array<float, 2> compressorSidechainHpfX1 { 0.0f, 0.0f };
    std::array<float, 2> compressorSidechainHpfY1 { 0.0f, 0.0f };
    float compressorSidechainHpfCoeff { 0.0f };
    std::array<float, 2> gateEnvelope { 0.0f, 0.0f };
    std::array<GranularPitchShifter, 2> octavePitchShifters {};
    std::array<DelayLine, 2> doublerDelayLines {};
    std::array<GranularPitchShifter, 2> doublerPitchShifters {};
    std::array<DelayLine, 2> delayDelayLines {};
    std::array<float, 2> distortionToneState { 0.0f, 0.0f };
    std::array<float, 2> fuzzToneState { 0.0f, 0.0f };

    juce::dsp::Reverb reverb;
    juce::dsp::Reverb::Parameters reverbParameters;

    juce::dsp::Oscillator<float> osc;
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleEQAudioProcessor)
};
