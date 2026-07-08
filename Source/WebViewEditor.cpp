#include "WebViewEditor.h"

#include <cmath>

namespace
{
constexpr auto frontendSetParameterEvent = "frontendSetParameter";
constexpr auto backendParametersEvent = "backendParameters";

juce::String getMimeTypeForExtension(const juce::String& extension)
{
    if (extension == ".html") return "text/html";
    if (extension == ".js" || extension == ".mjs") return "text/javascript";
    if (extension == ".css") return "text/css";
    if (extension == ".json") return "application/json";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".woff2") return "font/woff2";
    if (extension == ".woff") return "font/woff";
    return "application/octet-stream";
}

juce::var makeSnapshotVar(float inputGain,
                          float tunerReference,
                          float gateThreshold,
                          float compressorAmount,
                          float compressorTone,
                          float compressorLevel,
                          float octaveTranspose,
                          float doublerMix,
                          float doublerDelay,
                          float doublerDetune,
                          float delayMix,
                          float delayTimeL,
                          float delayTimeR,
                          float delayFeedback,
                          bool  delayModeIsDual,
                          float drive,
                          float distortionTone,
                          float distortionLevel,
                          float fuzzDrive,
                          float fuzzTone,
                          float fuzzLevel,
                          float outputGain,
                          float lowCutFreq,
                          float highCutFreq,
                          float peakFreq,
                          float peakGain,
                          float peakQuality,
                          float lowCutSlope,
                          float highCutSlope,
                          float reverbSize,
                          float reverbDamping,
                          float reverbMix,
                          float reverbWidth,
                          bool lowCutBypassed,
                          bool peakBypassed,
                          bool highCutBypassed,
                          bool driveBypassed,
                          bool fuzzBypassed,
                          bool compressorBypassed,
                          bool octaveBypassed,
                          bool doublerBypassed,
                          bool delayBypassed,
                          bool reverbBypassed,
                          bool tunerBypassed,
                          bool gateBypassed,
                          bool analyzerEnabled,
                          float inputPeak,
                          float outputPeak,
                          bool inputClipping,
                          bool outputClipping,
                          juce::var tunerFrequency,
                          juce::var tunerCents,
                          juce::var tunerNoteIndex,
                          juce::var tunerLevel,
                          juce::var responseCurve,
                          juce::var leftSpectrum,
                          juce::var rightSpectrum)
{
    auto payload = std::make_unique<juce::DynamicObject>();
    payload->setProperty("inputGain", inputGain);
    payload->setProperty("tunerReference", tunerReference);
    payload->setProperty("gateThreshold", gateThreshold);
    payload->setProperty("compressorAmount", compressorAmount);
    payload->setProperty("compressorTone", compressorTone);
    payload->setProperty("compressorLevel", compressorLevel);
    payload->setProperty("octaveTranspose", octaveTranspose);
    payload->setProperty("doublerMix", doublerMix);
    payload->setProperty("doublerDelay", doublerDelay);
    payload->setProperty("doublerDetune", doublerDetune);
    payload->setProperty("delayMix", delayMix);
    payload->setProperty("delayTimeL", delayTimeL);
    payload->setProperty("delayTimeR", delayTimeR);
    payload->setProperty("delayFeedback", delayFeedback);
    payload->setProperty("delayModeIsDual", delayModeIsDual);
    payload->setProperty("drive", drive);
    payload->setProperty("distortionTone", distortionTone);
    payload->setProperty("distortionLevel", distortionLevel);
    payload->setProperty("fuzzDrive", fuzzDrive);
    payload->setProperty("fuzzTone", fuzzTone);
    payload->setProperty("fuzzLevel", fuzzLevel);
    payload->setProperty("outputGain", outputGain);
    payload->setProperty("lowCutFreq", lowCutFreq);
    payload->setProperty("highCutFreq", highCutFreq);
    payload->setProperty("peakFreq", peakFreq);
    payload->setProperty("peakGain", peakGain);
    payload->setProperty("peakQuality", peakQuality);
    payload->setProperty("lowCutSlope", lowCutSlope);
    payload->setProperty("highCutSlope", highCutSlope);
    payload->setProperty("reverbSize", reverbSize);
    payload->setProperty("reverbDamping", reverbDamping);
    payload->setProperty("reverbMix", reverbMix);
    payload->setProperty("reverbWidth", reverbWidth);
    payload->setProperty("lowCutBypassed", lowCutBypassed);
    payload->setProperty("peakBypassed", peakBypassed);
    payload->setProperty("highCutBypassed", highCutBypassed);
    payload->setProperty("driveBypassed", driveBypassed);
    payload->setProperty("fuzzBypassed", fuzzBypassed);
    payload->setProperty("compressorBypassed", compressorBypassed);
    payload->setProperty("octaveBypassed", octaveBypassed);
    payload->setProperty("doublerBypassed", doublerBypassed);
    payload->setProperty("delayBypassed", delayBypassed);
    payload->setProperty("reverbBypassed", reverbBypassed);
    payload->setProperty("tunerBypassed", tunerBypassed);
    payload->setProperty("gateBypassed", gateBypassed);
    payload->setProperty("analyzerEnabled", analyzerEnabled);
    payload->setProperty("inputPeak", inputPeak);
    payload->setProperty("outputPeak", outputPeak);
    payload->setProperty("inputClipping", inputClipping);
    payload->setProperty("outputClipping", outputClipping);

    auto tuner = std::make_unique<juce::DynamicObject>();
    tuner->setProperty("frequency", tunerFrequency);
    tuner->setProperty("cents", tunerCents);
    tuner->setProperty("noteIndex", tunerNoteIndex);
    tuner->setProperty("level", tunerLevel);
    payload->setProperty("tuner", juce::var(tuner.release()));

    payload->setProperty("responseCurve", responseCurve);
    payload->setProperty("leftSpectrum", leftSpectrum);
    payload->setProperty("rightSpectrum", rightSpectrum);
    return juce::var(payload.release());
}

juce::File findLocalWebUiDistIndex()
{
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

    auto current = appFile.getParentDirectory();
    for (int i = 0; i < 10; ++i)
    {
        auto candidate = current.getChildFile("WebUI").getChildFile("dist").getChildFile("index.html");
        if (candidate.existsAsFile())
            return candidate;

        if (current == current.getParentDirectory())
            break;

        current = current.getParentDirectory();
    }

    auto cwd = juce::File::getCurrentWorkingDirectory();
    for (int i = 0; i < 5; ++i)
    {
        auto candidate = cwd.getChildFile("WebUI").getChildFile("dist").getChildFile("index.html");
        if (candidate.existsAsFile())
            return candidate;

        if (cwd == cwd.getParentDirectory())
            break;

        cwd = cwd.getParentDirectory();
    }

    return {};
}

juce::WebBrowserComponent::Options makeWebViewOptions(SimpleEQAudioProcessor& processor,
                                                      const juce::File& distRoot)
{
    auto input = processor.apvts.getRawParameterValue("Input Gain")->load();
    auto tunerReference = processor.apvts.getRawParameterValue("Tuner Reference")->load();
    auto gateThreshold = processor.apvts.getRawParameterValue("Gate Threshold")->load();
    auto compressorAmount = processor.apvts.getRawParameterValue("Compressor Amount")->load();
    auto compressorTone = processor.apvts.getRawParameterValue("Compressor Tone")->load();
    auto compressorLevel = processor.apvts.getRawParameterValue("Compressor Level")->load();
    auto octaveTranspose = processor.apvts.getRawParameterValue("Octave Transpose")->load();
    auto doublerMix = processor.apvts.getRawParameterValue("Doubler Mix")->load();
    auto doublerDelay = processor.apvts.getRawParameterValue("Doubler Delay")->load();
    auto doublerDetune = processor.apvts.getRawParameterValue("Doubler Detune")->load();
    auto delayMix = processor.apvts.getRawParameterValue("Delay Mix")->load();
    auto delayTimeL = processor.apvts.getRawParameterValue("Delay Time L")->load();
    auto delayTimeR = processor.apvts.getRawParameterValue("Delay Time R")->load();
    auto delayFeedback = processor.apvts.getRawParameterValue("Delay Feedback")->load();
    bool delayModeIsDual = false;
    if (auto* modeParam = dynamic_cast<juce::AudioParameterChoice*>(
            processor.apvts.getParameter("Delay Mode")))
        delayModeIsDual = modeParam->getIndex() == 1;
    auto drive = processor.apvts.getRawParameterValue("Distortion Drive")->load();
    auto distortionTone = processor.apvts.getRawParameterValue("Distortion Tone")->load();
    auto distortionLevel = processor.apvts.getRawParameterValue("Distortion Level")->load();
    auto fuzzDrive = processor.apvts.getRawParameterValue("Fuzz Drive")->load();
    auto fuzzTone = processor.apvts.getRawParameterValue("Fuzz Tone")->load();
    auto fuzzLevel = processor.apvts.getRawParameterValue("Fuzz Level")->load();
    auto output = processor.apvts.getRawParameterValue("Output Gain")->load();
    auto lowCutFreq = processor.apvts.getRawParameterValue("LowCut Freq")->load();
    auto highCutFreq = processor.apvts.getRawParameterValue("HighCut Freq")->load();
    auto peakFreq = processor.apvts.getRawParameterValue("Peak Freq")->load();
    auto peakGain = processor.apvts.getRawParameterValue("Peak Gain")->load();
    auto peakQuality = processor.apvts.getRawParameterValue("Peak Quality")->load();
    auto lowCutSlope = processor.apvts.getRawParameterValue("LowCut Slope")->load();
    auto highCutSlope = processor.apvts.getRawParameterValue("HighCut Slope")->load();
    auto lowCutBypassed = processor.apvts.getRawParameterValue("LowCut Bypassed")->load() > 0.5f;
    auto peakBypassed = processor.apvts.getRawParameterValue("Peak Bypassed")->load() > 0.5f;
    auto highCutBypassed = processor.apvts.getRawParameterValue("HighCut Bypassed")->load() > 0.5f;
    auto driveBypassed = processor.apvts.getRawParameterValue("Distortion Bypassed")->load() > 0.5f;
    auto fuzzBypassed = processor.apvts.getRawParameterValue("Fuzz Bypassed")->load() > 0.5f;
    auto compressorBypassed = processor.apvts.getRawParameterValue("Compressor Bypassed")->load() > 0.5f;
    auto octaveBypassed = processor.apvts.getRawParameterValue("Octave Bypassed")->load() > 0.5f;
    auto doublerBypassed = processor.apvts.getRawParameterValue("Doubler Bypassed")->load() > 0.5f;
    auto delayBypassed = processor.apvts.getRawParameterValue("Delay Bypassed")->load() > 0.5f;
    auto reverbSize = processor.apvts.getRawParameterValue("Reverb Size")->load();
    auto reverbDamping = processor.apvts.getRawParameterValue("Reverb Damping")->load();
    auto reverbMix = processor.apvts.getRawParameterValue("Reverb Mix")->load();
    auto reverbWidth = processor.apvts.getRawParameterValue("Reverb Width")->load();
    auto reverbBypassed = processor.apvts.getRawParameterValue("Reverb Bypassed")->load() > 0.5f;
    auto tunerBypassed = processor.apvts.getRawParameterValue("Tuner Bypassed")->load() > 0.5f;
    auto gateBypassed = processor.apvts.getRawParameterValue("Gate Bypassed")->load() > 0.5f;
    auto analyzerEnabled = processor.apvts.getRawParameterValue("Analyzer Enabled")->load() > 0.5f;

    const auto userDataFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                    .getChildFile("SimpleEQWebView2Data");

    return juce::WebBrowserComponent::Options{}
        .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2{}
                                    .withUserDataFolder(userDataFolder)
                                    .withStatusBarDisabled())
        .withResourceProvider([distRoot](const juce::String& path) -> std::optional<juce::WebBrowserComponent::Resource>
        {
            if (!distRoot.isDirectory())
                return std::nullopt;

            auto relativePath = path;
            if (relativePath.isEmpty() || relativePath == "/")
                relativePath = "/index.html";

            relativePath = relativePath.fromFirstOccurrenceOf("/", false, false);
            auto targetFile = distRoot.getChildFile(relativePath);

            if (!targetFile.existsAsFile())
                return std::nullopt;

            juce::MemoryBlock bytes;
            if (!targetFile.loadFileAsData(bytes))
                return std::nullopt;

            std::vector<std::byte> data(static_cast<size_t>(bytes.getSize()));
            std::memcpy(data.data(), bytes.getData(), data.size());

            return juce::WebBrowserComponent::Resource {
                std::move(data),
                getMimeTypeForExtension(targetFile.getFileExtension())
            };
        },
        juce::WebBrowserComponent::getResourceProviderRoot().upToLastOccurrenceOf("/", false, false))
        .withNativeIntegrationEnabled(true)
        .withEventListener(frontendSetParameterEvent, [&processor](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const auto parameterID = obj->getProperty("id").toString();
                const auto value = static_cast<float>(obj->getProperty("value"));

                if (auto* parameter = processor.apvts.getParameter(parameterID))
                    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            }
        })
        .withInitialisationData("parameters", makeSnapshotVar(input,
                                    tunerReference,
                                    gateThreshold,
                                    compressorAmount,
                                    compressorTone,
                                    compressorLevel,
                                    octaveTranspose,
                                    doublerMix,
                                    doublerDelay,
                                    doublerDetune,
                                    delayMix,
                                    delayTimeL,
                                    delayTimeR,
                                    delayFeedback,
                                    delayModeIsDual,
                                    drive,
                                    distortionTone,
                                    distortionLevel,
                                    fuzzDrive,
                                    fuzzTone,
                                    fuzzLevel,
                                    output,
                                    lowCutFreq,
                                    highCutFreq,
                                    peakFreq,
                                    peakGain,
                                    peakQuality,
                                    lowCutSlope,
                                    highCutSlope,
                                    reverbSize,
                                    reverbDamping,
                                    reverbMix,
                                    reverbWidth,
                                    lowCutBypassed,
                                    peakBypassed,
                                    highCutBypassed,
                                    driveBypassed,
                                    fuzzBypassed,
                                    compressorBypassed,
                                    octaveBypassed,
                                    doublerBypassed,
                                    delayBypassed,
                                    reverbBypassed,
                                    tunerBypassed,
                                    gateBypassed,
                                    analyzerEnabled,
                                    0.0f,
                                    0.0f,
                                    false,
                                    false,
                                    0.0f,
                                    0.0f,
                                    -1,
                                    -60.0f,
                                    juce::var(),
                                    juce::var(),
                                    juce::var()));
}
}

SimpleEQWebViewEditor::SimpleEQWebViewEditor(SimpleEQAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
    , webView(makeWebViewOptions(p, getWebUiDistRoot()))
{
    addAndMakeVisible(webView);

    fallbackLabel.setJustificationType(juce::Justification::centred);
    fallbackLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    fallbackLabel.setText("SimpleEQ Web UI not found. Run: npm install && npm run build in WebUI.",
                          juce::dontSendNotification);
    addAndMakeVisible(fallbackLabel);

    if (!juce::WebBrowserComponent::areOptionsSupported(makeWebViewOptions(p, getWebUiDistRoot())))
    {
        webView.setVisible(false);
        fallbackLabel.setText("WebView2 backend is not available on this system. Install Microsoft Edge WebView2 Runtime.",
                              juce::dontSendNotification);
        setSize(960, 640);
        return;
    }

    const auto url = resolveWebUiUrl();
    if (url.isNotEmpty())
    {
        webView.goToURL(url);
        fallbackLabel.setVisible(false);
    }
    else
    {
        webView.setVisible(false);
        fallbackLabel.setVisible(true);
    }

    audioProcessor.apvts.addParameterListener(paramInputGain, this);
    audioProcessor.apvts.addParameterListener(paramTunerReference, this);
    audioProcessor.apvts.addParameterListener(paramTunerBypassed, this);
    audioProcessor.apvts.addParameterListener(paramGateThreshold, this);
    audioProcessor.apvts.addParameterListener(paramGateBypassed, this);
    audioProcessor.apvts.addParameterListener(paramCompressorAmount, this);
    audioProcessor.apvts.addParameterListener(paramCompressorTone, this);
    audioProcessor.apvts.addParameterListener(paramCompressorLevel, this);
    audioProcessor.apvts.addParameterListener(paramOctaveTranspose, this);
    audioProcessor.apvts.addParameterListener(paramDoublerMix, this);
    audioProcessor.apvts.addParameterListener(paramDoublerDelay, this);
    audioProcessor.apvts.addParameterListener(paramDoublerDetune, this);
    audioProcessor.apvts.addParameterListener(paramDoublerBypassed, this);
    audioProcessor.apvts.addParameterListener(paramDelayMix, this);
    audioProcessor.apvts.addParameterListener(paramDelayTimeL, this);
    audioProcessor.apvts.addParameterListener(paramDelayTimeR, this);
    audioProcessor.apvts.addParameterListener(paramDelayFeedback, this);
    audioProcessor.apvts.addParameterListener(paramDelayMode, this);
    audioProcessor.apvts.addParameterListener(paramDelayBypassed, this);
    audioProcessor.apvts.addParameterListener(paramOctaveBypassed, this);
    audioProcessor.apvts.addParameterListener(paramDrive, this);
    audioProcessor.apvts.addParameterListener(paramDistortionTone, this);
    audioProcessor.apvts.addParameterListener(paramDistortionLevel, this);
    audioProcessor.apvts.addParameterListener(paramFuzzDrive, this);
    audioProcessor.apvts.addParameterListener(paramFuzzTone, this);
    audioProcessor.apvts.addParameterListener(paramFuzzLevel, this);
    audioProcessor.apvts.addParameterListener(paramOutputGain, this);
    audioProcessor.apvts.addParameterListener(paramLowCutFreq, this);
    audioProcessor.apvts.addParameterListener(paramHighCutFreq, this);
    audioProcessor.apvts.addParameterListener(paramPeakFreq, this);
    audioProcessor.apvts.addParameterListener(paramPeakGain, this);
    audioProcessor.apvts.addParameterListener(paramPeakQuality, this);
    audioProcessor.apvts.addParameterListener(paramLowCutSlope, this);
    audioProcessor.apvts.addParameterListener(paramHighCutSlope, this);
    audioProcessor.apvts.addParameterListener(paramLowCutBypassed, this);
    audioProcessor.apvts.addParameterListener(paramPeakBypassed, this);
    audioProcessor.apvts.addParameterListener(paramHighCutBypassed, this);
    audioProcessor.apvts.addParameterListener(paramDriveBypassed, this);
    audioProcessor.apvts.addParameterListener(paramFuzzBypassed, this);
    audioProcessor.apvts.addParameterListener(paramCompressorBypassed, this);
    audioProcessor.apvts.addParameterListener(paramReverbSize, this);
    audioProcessor.apvts.addParameterListener(paramReverbDamping, this);
    audioProcessor.apvts.addParameterListener(paramReverbMix, this);
    audioProcessor.apvts.addParameterListener(paramReverbWidth, this);
    audioProcessor.apvts.addParameterListener(paramReverbBypassed, this);
    audioProcessor.apvts.addParameterListener(paramAnalyzerEnabled, this);

    startTimerHz(60);

    setSize(960, 640);
}

SimpleEQWebViewEditor::~SimpleEQWebViewEditor()
{
    cancelPendingUpdate();
    stopTimer();
    audioProcessor.apvts.removeParameterListener(paramInputGain, this);
    audioProcessor.apvts.removeParameterListener(paramTunerReference, this);
    audioProcessor.apvts.removeParameterListener(paramTunerBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramGateThreshold, this);
    audioProcessor.apvts.removeParameterListener(paramGateBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramCompressorAmount, this);
    audioProcessor.apvts.removeParameterListener(paramCompressorTone, this);
    audioProcessor.apvts.removeParameterListener(paramCompressorLevel, this);
    audioProcessor.apvts.removeParameterListener(paramOctaveTranspose, this);
    audioProcessor.apvts.removeParameterListener(paramDoublerMix, this);
    audioProcessor.apvts.removeParameterListener(paramDoublerDelay, this);
    audioProcessor.apvts.removeParameterListener(paramDoublerDetune, this);
    audioProcessor.apvts.removeParameterListener(paramDoublerBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramDelayMix, this);
    audioProcessor.apvts.removeParameterListener(paramDelayTimeL, this);
    audioProcessor.apvts.removeParameterListener(paramDelayTimeR, this);
    audioProcessor.apvts.removeParameterListener(paramDelayFeedback, this);
    audioProcessor.apvts.removeParameterListener(paramDelayMode, this);
    audioProcessor.apvts.removeParameterListener(paramDelayBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramOctaveBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramDrive, this);
    audioProcessor.apvts.removeParameterListener(paramDistortionTone, this);
    audioProcessor.apvts.removeParameterListener(paramDistortionLevel, this);
    audioProcessor.apvts.removeParameterListener(paramFuzzDrive, this);
    audioProcessor.apvts.removeParameterListener(paramFuzzTone, this);
    audioProcessor.apvts.removeParameterListener(paramFuzzLevel, this);
    audioProcessor.apvts.removeParameterListener(paramOutputGain, this);
    audioProcessor.apvts.removeParameterListener(paramLowCutFreq, this);
    audioProcessor.apvts.removeParameterListener(paramHighCutFreq, this);
    audioProcessor.apvts.removeParameterListener(paramPeakFreq, this);
    audioProcessor.apvts.removeParameterListener(paramPeakGain, this);
    audioProcessor.apvts.removeParameterListener(paramPeakQuality, this);
    audioProcessor.apvts.removeParameterListener(paramLowCutSlope, this);
    audioProcessor.apvts.removeParameterListener(paramHighCutSlope, this);
    audioProcessor.apvts.removeParameterListener(paramLowCutBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramPeakBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramHighCutBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramDriveBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramFuzzBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramCompressorBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramReverbSize, this);
    audioProcessor.apvts.removeParameterListener(paramReverbDamping, this);
    audioProcessor.apvts.removeParameterListener(paramReverbMix, this);
    audioProcessor.apvts.removeParameterListener(paramReverbWidth, this);
    audioProcessor.apvts.removeParameterListener(paramReverbBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramAnalyzerEnabled, this);
}

void SimpleEQWebViewEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void SimpleEQWebViewEditor::resized()
{
    webView.setBounds(getLocalBounds());
    fallbackLabel.setBounds(getLocalBounds().reduced(20));
}

juce::String SimpleEQWebViewEditor::resolveWebUiUrl() const
{
    auto overrideUrl = juce::SystemStats::getEnvironmentVariable("SIMPLEEQ_WEB_UI_URL", {});
    if (overrideUrl.isNotEmpty())
        return overrideUrl;

    auto distRoot = getWebUiDistRoot();
    if (distRoot.isDirectory())
        return juce::WebBrowserComponent::getResourceProviderRoot();

    return {};
}

juce::File SimpleEQWebViewEditor::getWebUiDistRoot() const
{
    auto localIndex = findLocalWebUiDistIndex();
    if (localIndex.existsAsFile())
        return localIndex.getParentDirectory();

    return {};
}

void SimpleEQWebViewEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);

    if (parameterID != paramInputGain
        && parameterID != paramTunerReference
        && parameterID != paramTunerBypassed
        && parameterID != paramGateThreshold
        && parameterID != paramGateBypassed
        && parameterID != paramCompressorAmount
        && parameterID != paramCompressorTone
        && parameterID != paramCompressorLevel
        && parameterID != paramOctaveTranspose
        && parameterID != paramDoublerMix
        && parameterID != paramDoublerDelay
        && parameterID != paramDoublerDetune
        && parameterID != paramDoublerBypassed
        && parameterID != paramDelayMix
        && parameterID != paramDelayTimeL
        && parameterID != paramDelayTimeR
        && parameterID != paramDelayFeedback
        && parameterID != paramDelayMode
        && parameterID != paramDelayBypassed
        && parameterID != paramOctaveBypassed
        && parameterID != paramDrive
        && parameterID != paramDistortionTone
        && parameterID != paramDistortionLevel
        && parameterID != paramFuzzDrive
        && parameterID != paramFuzzTone
        && parameterID != paramFuzzLevel
        && parameterID != paramOutputGain
        && parameterID != paramLowCutFreq
        && parameterID != paramHighCutFreq
        && parameterID != paramPeakFreq
        && parameterID != paramPeakGain
        && parameterID != paramPeakQuality
        && parameterID != paramLowCutSlope
        && parameterID != paramHighCutSlope
        && parameterID != paramLowCutBypassed
        && parameterID != paramPeakBypassed
        && parameterID != paramHighCutBypassed
        && parameterID != paramDriveBypassed
        && parameterID != paramFuzzBypassed
        && parameterID != paramCompressorBypassed
        && parameterID != paramReverbSize
        && parameterID != paramReverbDamping
        && parameterID != paramReverbMix
        && parameterID != paramReverbWidth
        && parameterID != paramReverbBypassed
        && parameterID != paramAnalyzerEnabled)
        return;

    parameterUpdatePending.store(true, std::memory_order_release);
    triggerAsyncUpdate();
}

void SimpleEQWebViewEditor::handleAsyncUpdate()
{
    if (!parameterUpdatePending.exchange(false, std::memory_order_acq_rel))
        return;

    emitParameterSnapshotToFrontend();
}

void SimpleEQWebViewEditor::timerCallback()
{
    emitParameterSnapshotToFrontend();
}

void SimpleEQWebViewEditor::emitParameterSnapshotToFrontend()
{
    webView.emitEventIfBrowserIsVisible(juce::Identifier(backendParametersEvent), makeParameterSnapshot());
}

juce::var SimpleEQWebViewEditor::makeParameterSnapshot()
{
    auto input = audioProcessor.apvts.getRawParameterValue(paramInputGain)->load();
    auto tunerReference = audioProcessor.apvts.getRawParameterValue(paramTunerReference)->load();
    auto gateThreshold = audioProcessor.apvts.getRawParameterValue(paramGateThreshold)->load();
    auto compressorAmount = audioProcessor.apvts.getRawParameterValue(paramCompressorAmount)->load();
    auto compressorTone = audioProcessor.apvts.getRawParameterValue(paramCompressorTone)->load();
    auto compressorLevel = audioProcessor.apvts.getRawParameterValue(paramCompressorLevel)->load();
    auto octaveTranspose = audioProcessor.apvts.getRawParameterValue(paramOctaveTranspose)->load();
    auto doublerMix = audioProcessor.apvts.getRawParameterValue(paramDoublerMix)->load();
    auto doublerDelay = audioProcessor.apvts.getRawParameterValue(paramDoublerDelay)->load();
    auto doublerDetune = audioProcessor.apvts.getRawParameterValue(paramDoublerDetune)->load();
    auto delayMix = audioProcessor.apvts.getRawParameterValue(paramDelayMix)->load();
    auto delayTimeL = audioProcessor.apvts.getRawParameterValue(paramDelayTimeL)->load();
    auto delayTimeR = audioProcessor.apvts.getRawParameterValue(paramDelayTimeR)->load();
    auto delayFeedback = audioProcessor.apvts.getRawParameterValue(paramDelayFeedback)->load();
    bool delayModeIsDual = false;
    if (auto* modeParam = dynamic_cast<juce::AudioParameterChoice*>(
            audioProcessor.apvts.getParameter(paramDelayMode)))
        delayModeIsDual = modeParam->getIndex() == 1;
    auto drive = audioProcessor.apvts.getRawParameterValue(paramDrive)->load();
    auto distortionTone = audioProcessor.apvts.getRawParameterValue(paramDistortionTone)->load();
    auto distortionLevel = audioProcessor.apvts.getRawParameterValue(paramDistortionLevel)->load();
    auto fuzzDrive = audioProcessor.apvts.getRawParameterValue(paramFuzzDrive)->load();
    auto fuzzTone = audioProcessor.apvts.getRawParameterValue(paramFuzzTone)->load();
    auto fuzzLevel = audioProcessor.apvts.getRawParameterValue(paramFuzzLevel)->load();
    auto output = audioProcessor.apvts.getRawParameterValue(paramOutputGain)->load();
    auto lowCutFreq = audioProcessor.apvts.getRawParameterValue(paramLowCutFreq)->load();
    auto highCutFreq = audioProcessor.apvts.getRawParameterValue(paramHighCutFreq)->load();
    auto peakFreq = audioProcessor.apvts.getRawParameterValue(paramPeakFreq)->load();
    auto peakGain = audioProcessor.apvts.getRawParameterValue(paramPeakGain)->load();
    auto peakQuality = audioProcessor.apvts.getRawParameterValue(paramPeakQuality)->load();
    auto lowCutSlope = audioProcessor.apvts.getRawParameterValue(paramLowCutSlope)->load();
    auto highCutSlope = audioProcessor.apvts.getRawParameterValue(paramHighCutSlope)->load();
    auto lowCutBypassed = audioProcessor.apvts.getRawParameterValue(paramLowCutBypassed)->load() > 0.5f;
    auto peakBypassed = audioProcessor.apvts.getRawParameterValue(paramPeakBypassed)->load() > 0.5f;
    auto highCutBypassed = audioProcessor.apvts.getRawParameterValue(paramHighCutBypassed)->load() > 0.5f;
    auto driveBypassed = audioProcessor.apvts.getRawParameterValue(paramDriveBypassed)->load() > 0.5f;
    auto fuzzBypassed = audioProcessor.apvts.getRawParameterValue(paramFuzzBypassed)->load() > 0.5f;
    auto compressorBypassed = audioProcessor.apvts.getRawParameterValue(paramCompressorBypassed)->load() > 0.5f;
    auto octaveBypassed = audioProcessor.apvts.getRawParameterValue(paramOctaveBypassed)->load() > 0.5f;
    auto doublerBypassed = audioProcessor.apvts.getRawParameterValue(paramDoublerBypassed)->load() > 0.5f;
    auto delayBypassed = audioProcessor.apvts.getRawParameterValue(paramDelayBypassed)->load() > 0.5f;
    auto reverbSize = audioProcessor.apvts.getRawParameterValue(paramReverbSize)->load();
    auto reverbDamping = audioProcessor.apvts.getRawParameterValue(paramReverbDamping)->load();
    auto reverbMix = audioProcessor.apvts.getRawParameterValue(paramReverbMix)->load();
    auto reverbWidth = audioProcessor.apvts.getRawParameterValue(paramReverbWidth)->load();
    auto reverbBypassed = audioProcessor.apvts.getRawParameterValue(paramReverbBypassed)->load() > 0.5f;
    auto tunerBypassed = audioProcessor.apvts.getRawParameterValue(paramTunerBypassed)->load() > 0.5f;
    auto gateBypassed = audioProcessor.apvts.getRawParameterValue(paramGateBypassed)->load() > 0.5f;
    auto analyzerEnabled = audioProcessor.apvts.getRawParameterValue(paramAnalyzerEnabled)->load() > 0.5f;
    auto inputPeak = audioProcessor.consumeInputPeakLevel();
    auto outputPeak = audioProcessor.consumeOutputPeakLevel();
    auto inputClipping = audioProcessor.consumeInputClippingFlag();
    auto outputClipping = audioProcessor.consumeOutputClippingFlag();
    auto responseCurve = buildResponseCurve();
    auto leftSpectrum = buildSpectrumFromFifo(audioProcessor.leftChannelFifo, leftMonoBuffer, leftFftScratch, leftFftResult);
    auto rightSpectrum = buildSpectrumFromFifo(audioProcessor.rightChannelFifo, rightMonoBuffer, rightFftScratch, rightFftResult);

    const auto tunerFrequency = audioProcessor.getTunerDetectedFrequencyHz();
    const auto tunerCents = audioProcessor.getTunerDetectedCents();
    const auto tunerNoteIndex = audioProcessor.getTunerDetectedNoteIndex();
    const auto tunerLevel = audioProcessor.getTunerDetectedLevelDb();

    return makeSnapshotVar(input,
                           tunerReference,
                           gateThreshold,
                           compressorAmount,
                           compressorTone,
                           compressorLevel,
                           octaveTranspose,
                           doublerMix,
                           doublerDelay,
                           doublerDetune,
                           delayMix,
                           delayTimeL,
                           delayTimeR,
                           delayFeedback,
                           delayModeIsDual,
                           drive,
                           distortionTone,
                           distortionLevel,
                           fuzzDrive,
                           fuzzTone,
                           fuzzLevel,
                           output,
                           lowCutFreq,
                           highCutFreq,
                           peakFreq,
                           peakGain,
                           peakQuality,
                           lowCutSlope,
                           highCutSlope,
                           reverbSize,
                           reverbDamping,
                           reverbMix,
                           reverbWidth,
                           lowCutBypassed,
                           peakBypassed,
                           highCutBypassed,
                           driveBypassed,
                           fuzzBypassed,
                           compressorBypassed,
                           octaveBypassed,
                           doublerBypassed,
                           delayBypassed,
                           reverbBypassed,
                           tunerBypassed,
                           gateBypassed,
                           analyzerEnabled,
                           inputPeak,
                           outputPeak,
                           inputClipping,
                           outputClipping,
                           tunerFrequency,
                           tunerCents,
                           tunerNoteIndex,
                           tunerLevel,
                           makeFloatArrayVar(responseCurve),
                           makeFloatArrayVar(leftSpectrum),
                           makeFloatArrayVar(rightSpectrum));
}

juce::var SimpleEQWebViewEditor::makeFloatArrayVar(const std::vector<float>& values)
{
    juce::Array<juce::var> array;
    array.ensureStorageAllocated(static_cast<int>(values.size()));
    for (auto value : values)
        array.add(value);

    return juce::var(array);
}

std::vector<float> SimpleEQWebViewEditor::buildSpectrumFromFifo(SingleChannelSampleFifo<SimpleEQAudioProcessor::BlockType>& fifo,
                                                                 juce::AudioBuffer<float>& monoBuffer,
                                                                 std::vector<float>& fftScratch,
                                                                 std::vector<float>& fftResult)
{
    SimpleEQAudioProcessor::BlockType tempIncomingBuffer;
    bool hasNewData = false;

    while (fifo.getNumCompleteBuffersAvailable() > 0)
    {
        if (fifo.getAudioBuffer(tempIncomingBuffer))
        {
            hasNewData = true;

            const auto incomingSize = tempIncomingBuffer.getNumSamples();
            const auto size = juce::jmin(incomingSize, monoBuffer.getNumSamples());

            if (size < monoBuffer.getNumSamples())
            {
                juce::FloatVectorOperations::copy(monoBuffer.getWritePointer(0, 0),
                                                  monoBuffer.getReadPointer(0, size),
                                                  monoBuffer.getNumSamples() - size);
            }

            juce::FloatVectorOperations::copy(monoBuffer.getWritePointer(0, monoBuffer.getNumSamples() - size),
                                              tempIncomingBuffer.getReadPointer(0, incomingSize - size),
                                              size);
        }
    }

    if (!hasNewData)
        return fftResult;

    std::fill(fftScratch.begin(), fftScratch.end(), 0.0f);
    std::copy(monoBuffer.getReadPointer(0),
              monoBuffer.getReadPointer(0) + fftSize,
              fftScratch.begin());

    analyzerWindow.multiplyWithWindowingTable(fftScratch.data(), fftSize);
    analyzerFft.performFrequencyOnlyForwardTransform(fftScratch.data());

    const auto numBins = fftSize / 2;
    for (int i = 0; i < numBins; ++i)
    {
        auto v = fftScratch[i];
        if (!std::isinf(v) && !std::isnan(v))
            v /= static_cast<float>(numBins);
        else
            v = 0.0f;

        fftScratch[i] = juce::Decibels::gainToDecibels(v, spectrumNegativeInfinity);
    }

    const auto sampleRate = juce::jmax(1.0, audioProcessor.getSampleRate());
    const auto binWidth = sampleRate / static_cast<double>(fftSize);

    for (int i = 0; i < spectrumPoints; ++i)
    {
        const auto normalizedX = spectrumPoints == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(spectrumPoints - 1);
        const auto frequency = juce::mapToLog10(normalizedX, 20.0f, 20000.0f);
        const auto binNum = juce::jlimit(0, numBins - 1, static_cast<int>(std::round(frequency / binWidth)));

        const auto targetDb = fftScratch[binNum];
        auto& currentDb = fftResult[static_cast<size_t>(i)];

        if (targetDb >= currentDb)
            currentDb = targetDb;
        else
            currentDb = 0.75f * currentDb + 0.25f * targetDb;
    }

    return fftResult;
}

std::vector<float> SimpleEQWebViewEditor::buildResponseCurve() const
{
    auto settings = getChainSettings(audioProcessor.apvts);
    auto sampleRate = juce::jmax(1.0, audioProcessor.getSampleRate());

    auto lowCut = makeLowCutFilter(settings, sampleRate);
    auto highCut = makeHighCutFilter(settings, sampleRate);
    auto peak = makePeakFilter(settings, sampleRate);

    std::vector<float> magnitudes(static_cast<size_t>(spectrumPoints), 0.0f);

    for (int i = 0; i < spectrumPoints; ++i)
    {
        const auto normalizedX = spectrumPoints == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(spectrumPoints - 1);
        const auto frequency = juce::mapToLog10(normalizedX, 20.0f, 20000.0f);

        double mag = 1.0;

        if (!settings.lowCutBypassed)
        {
            const auto sections = static_cast<int>(settings.lowCutSlope) + 1;
            for (int section = 0; section < sections; ++section)
                mag *= lowCut[static_cast<size_t>(section)]->getMagnitudeForFrequency(frequency, sampleRate);
        }

        if (!settings.peakBypassed)
            mag *= peak->getMagnitudeForFrequency(frequency, sampleRate);

        if (!settings.highCutBypassed)
        {
            const auto sections = static_cast<int>(settings.highCutSlope) + 1;
            for (int section = 0; section < sections; ++section)
                mag *= highCut[static_cast<size_t>(section)]->getMagnitudeForFrequency(frequency, sampleRate);
        }

        magnitudes[static_cast<size_t>(i)] = juce::Decibels::gainToDecibels(static_cast<float>(mag), -24.0f);
    }

    return magnitudes;
}
