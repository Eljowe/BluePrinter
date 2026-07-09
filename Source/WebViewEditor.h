#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>

class SimpleEQWebViewEditor : public juce::AudioProcessorEditor
                              , private juce::AudioProcessorValueTreeState::Listener
                              , private juce::AsyncUpdater
                              , private juce::Timer
{
public:
    explicit SimpleEQWebViewEditor(SimpleEQAudioProcessor&);
    ~SimpleEQWebViewEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    juce::String resolveWebUiUrl() const;
    juce::File getWebUiDistRoot() const;
    void emitParameterSnapshotToFrontend();
    juce::var makeParameterSnapshot();
    std::vector<float> buildSpectrumFromFifo(SingleChannelSampleFifo<SimpleEQAudioProcessor::BlockType>& fifo,
                                             juce::AudioBuffer<float>& monoBuffer,
                                             std::vector<float>& fftScratch,
                                             std::vector<float>& fftResult);
    std::vector<float> buildResponseCurve() const;
    static juce::var makeFloatArrayVar(const std::vector<float>& values);

    static constexpr const char* paramInputGain = "Input Gain";
    static constexpr const char* paramTunerReference = "Tuner Reference";
    static constexpr const char* paramTunerBypassed = "Tuner Bypassed";
    static constexpr const char* paramGateThreshold = "Gate Threshold";
    static constexpr const char* paramGateBypassed = "Gate Bypassed";
    static constexpr const char* paramCompressorAmount = "Compressor Amount";
    static constexpr const char* paramCompressorTone = "Compressor Tone";
    static constexpr const char* paramCompressorLevel = "Compressor Level";
    static constexpr const char* paramOctaveTranspose = "Octave Transpose";
    static constexpr const char* paramOctaveMix = "Octave Mix";
    static constexpr const char* paramOctaveTone = "Octave Tone";
    static constexpr const char* paramOctaveMonoDetector = "Octave Mono Detector";
    static constexpr const char* paramOctaveBypassed = "Octave Bypassed";
    static constexpr const char* paramDoublerMix = "Doubler Mix";
    static constexpr const char* paramDoublerDelay = "Doubler Delay";
    static constexpr const char* paramDoublerDetune = "Doubler Detune";
    static constexpr const char* paramDoublerBypassed = "Doubler Bypassed";
    static constexpr const char* paramTremoloSpeed = "Tremolo Speed";
    static constexpr const char* paramTremoloDepth = "Tremolo Depth";
    static constexpr const char* paramTremoloLfo = "Tremolo LFO";
    static constexpr const char* paramTremoloStereoPhase = "Tremolo Stereo Phase";
    static constexpr const char* paramTremoloBypassed = "Tremolo Bypassed";
    static constexpr const char* paramDelayMix = "Delay Mix";
    static constexpr const char* paramDelayTimeL = "Delay Time L";
    static constexpr const char* paramDelayTimeR = "Delay Time R";
    static constexpr const char* paramDelayFeedback = "Delay Feedback";
    static constexpr const char* paramDelayMode = "Delay Mode";
    static constexpr const char* paramDelayBypassed = "Delay Bypassed";
    static constexpr const char* paramOverdriveDrive = "Overdrive Drive";
    static constexpr const char* paramOverdriveTone = "Overdrive Tone";
    static constexpr const char* paramOverdriveLevel = "Overdrive Level";
    static constexpr const char* paramDrive = "Distortion Drive";
    static constexpr const char* paramDistortionTone = "Distortion Tone";
    static constexpr const char* paramDistortionLevel = "Distortion Level";
    static constexpr const char* paramFuzzDrive = "Fuzz Drive";
    static constexpr const char* paramFuzzTone = "Fuzz Tone";
    static constexpr const char* paramFuzzLevel = "Fuzz Level";
    static constexpr const char* paramSynthFuzzMix = "Synth Fuzz Mix";
    static constexpr const char* paramSynthFuzzDelay = "Synth Fuzz Delay";
    static constexpr const char* paramSynthFuzzDetune = "Synth Fuzz Detune";
    static constexpr const char* paramSynthFuzzDrive = "Synth Fuzz Drive";
    static constexpr const char* paramSynthFuzzLevel = "Synth Fuzz Level";
    static constexpr const char* paramSynthFuzzBypassed = "Synth Fuzz Bypassed";
public:
    static constexpr const char* paramOutputGain = "Output Gain";
    static constexpr const char* paramOverdriveBypassed = "Overdrive Bypassed";
    static constexpr const char* paramDriveBypassed = "Distortion Bypassed";
    static constexpr const char* paramFuzzBypassed = "Fuzz Bypassed";
    static constexpr const char* paramCompressorBypassed = "Compressor Bypassed";
    static constexpr const char* paramReverbSize = "Reverb Size";
    static constexpr const char* paramReverbDamping = "Reverb Damping";
    static constexpr const char* paramReverbMix = "Reverb Mix";
    static constexpr const char* paramReverbWidth = "Reverb Width";
    static constexpr const char* paramReverbBypassed = "Reverb Bypassed";
    static constexpr const char* paramAnalyzerEnabled = "Analyzer Enabled";

    SimpleEQAudioProcessor& audioProcessor;
    juce::WebBrowserComponent webView;
    juce::Label fallbackLabel;

    std::atomic<bool> parameterUpdatePending { false };

    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int spectrumPoints = 96;
    static constexpr float spectrumNegativeInfinity = -48.0f;

    juce::dsp::FFT analyzerFft { fftOrder };
    juce::dsp::WindowingFunction<float> analyzerWindow { fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris };
    juce::AudioBuffer<float> leftMonoBuffer { 1, fftSize };
    juce::AudioBuffer<float> rightMonoBuffer { 1, fftSize };
    std::vector<float> leftFftScratch = std::vector<float>(fftSize * 2, 0.0f);
    std::vector<float> rightFftScratch = std::vector<float>(fftSize * 2, 0.0f);
    std::vector<float> leftFftResult = std::vector<float>(spectrumPoints, spectrumNegativeInfinity);
    std::vector<float> rightFftResult = std::vector<float>(spectrumPoints, spectrumNegativeInfinity);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleEQWebViewEditor)
};
