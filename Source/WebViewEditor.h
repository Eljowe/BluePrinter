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
                                             std::vector<float>& fftScratch,
                                             std::vector<float>& fftResult);
    std::vector<float> buildResponseCurve() const;
    static juce::var makeFloatArrayVar(const std::vector<float>& values);

    static constexpr const char* paramInputGain = "Input Gain";
    static constexpr const char* paramCompressorAmount = "Compressor Amount";
    static constexpr const char* paramDrive = "Distortion Drive";
    static constexpr const char* paramOutputGain = "Output Gain";
    static constexpr const char* paramLowCutFreq = "LowCut Freq";
    static constexpr const char* paramHighCutFreq = "HighCut Freq";
    static constexpr const char* paramPeakFreq = "Peak Freq";
    static constexpr const char* paramPeakGain = "Peak Gain";
    static constexpr const char* paramPeakQuality = "Peak Quality";
    static constexpr const char* paramLowCutSlope = "LowCut Slope";
    static constexpr const char* paramHighCutSlope = "HighCut Slope";
    static constexpr const char* paramLowCutBypassed = "LowCut Bypassed";
    static constexpr const char* paramPeakBypassed = "Peak Bypassed";
    static constexpr const char* paramHighCutBypassed = "HighCut Bypassed";
    static constexpr const char* paramDriveBypassed = "Distortion Bypassed";
    static constexpr const char* paramCompressorBypassed = "Compressor Bypassed";

    SimpleEQAudioProcessor& audioProcessor;
    juce::WebBrowserComponent webView;
    juce::Label fallbackLabel;

    std::atomic<bool> parameterUpdatePending { false };

    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int spectrumPoints = 96;

    juce::dsp::FFT analyzerFft { fftOrder };
    juce::dsp::WindowingFunction<float> analyzerWindow { fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> leftFftScratch = std::vector<float>(fftSize * 2, 0.0f);
    std::vector<float> rightFftScratch = std::vector<float>(fftSize * 2, 0.0f);
    std::vector<float> leftFftResult = std::vector<float>(spectrumPoints, -120.0f);
    std::vector<float> rightFftResult = std::vector<float>(spectrumPoints, -120.0f);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleEQWebViewEditor)
};
