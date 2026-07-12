#include "WebViewEditor.h"

#include <cmath>

namespace
{
constexpr auto frontendSetParameterEvent = "frontendSetParameter";
constexpr auto backendParametersEvent = "backendParameters";
constexpr int  numEqBands = 10;
constexpr auto paramEqBypassed = "EQ Bypassed";
juce::String paramEqBand(int index) { return "EQ Band " + juce::String(index); }
constexpr auto paramMonoInput = "Mono Input";
constexpr auto paramMute = "Mute";

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
                          float outputGain,
                          bool  monoInput,
                          bool  mute,
                          float tunerReference,
                          float gateThreshold,
                          float compressorAmount,
                          float compressorTone,
                          float compressorLevel,
                          float octaveTranspose,
                          float octaveMix,
                          float octaveTone,
                          bool  octaveMonoDetector,
                          float doublerMix,
                          float doublerDelay,
                          float doublerDetune,
                          float tremoloSpeed,
                          float tremoloDepth,
                          int   tremoloLfoIndex,
                          bool  tremoloStereoPhase,
                          float delayMix,
                          float delayTimeL,
                          float delayTimeR,
                          float delayFeedback,
                          bool  delayModeIsDual,
                          float overdriveDrive,
                          float overdriveTone,
                          float overdriveLevel,
                          float drive,
                          float distortionTone,
                          float distortionLevel,
                          float fuzzDrive,
                           float fuzzTone,
                           float fuzzLevel,
                          float synthFuzzMix,
                          float synthFuzzDelay,
                          float synthFuzzDetune,
                          float synthFuzzDrive,
                          float synthFuzzLevel,
                          juce::var eqBands,
                          float reverbSize,
                          float reverbDamping,
                          float reverbMix,
                          float reverbWidth,
                          bool eqBypassed,
                          bool overdriveBypassed,
                          bool driveBypassed,
                          bool fuzzBypassed,
                          bool synthFuzzBypassed,
                          bool compressorBypassed,
                          bool octaveBypassed,
                          bool doublerBypassed,
                          bool tremoloBypassed,
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
    payload->setProperty("outputGain", outputGain);
    payload->setProperty("monoInput", monoInput);
    payload->setProperty("mute", mute);
    payload->setProperty("tunerReference", tunerReference);
    payload->setProperty("gateThreshold", gateThreshold);
    payload->setProperty("compressorAmount", compressorAmount);
    payload->setProperty("compressorTone", compressorTone);
    payload->setProperty("compressorLevel", compressorLevel);
    payload->setProperty("octaveTranspose", octaveTranspose);
    payload->setProperty("octaveMix", octaveMix);
    payload->setProperty("octaveTone", octaveTone);
    payload->setProperty("octaveMonoDetector", octaveMonoDetector);
    payload->setProperty("doublerMix", doublerMix);
    payload->setProperty("doublerDelay", doublerDelay);
    payload->setProperty("doublerDetune", doublerDetune);
    payload->setProperty("tremoloSpeed", tremoloSpeed);
    payload->setProperty("tremoloDepth", tremoloDepth);
    payload->setProperty("tremoloLfoIndex", tremoloLfoIndex);
    payload->setProperty("tremoloStereoPhase", tremoloStereoPhase);
    payload->setProperty("delayMix", delayMix);
    payload->setProperty("delayTimeL", delayTimeL);
    payload->setProperty("delayTimeR", delayTimeR);
    payload->setProperty("delayFeedback", delayFeedback);
    payload->setProperty("delayModeIsDual", delayModeIsDual);
    payload->setProperty("overdriveDrive", overdriveDrive);
    payload->setProperty("overdriveTone", overdriveTone);
    payload->setProperty("overdriveLevel", overdriveLevel);
    payload->setProperty("drive", drive);
    payload->setProperty("distortionTone", distortionTone);
    payload->setProperty("distortionLevel", distortionLevel);
    payload->setProperty("fuzzDrive", fuzzDrive);
    payload->setProperty("fuzzTone", fuzzTone);
    payload->setProperty("fuzzLevel", fuzzLevel);
    payload->setProperty("synthFuzzMix", synthFuzzMix);
    payload->setProperty("synthFuzzDelay", synthFuzzDelay);
    payload->setProperty("synthFuzzDetune", synthFuzzDetune);
    payload->setProperty("synthFuzzDrive", synthFuzzDrive);
    payload->setProperty("synthFuzzLevel", synthFuzzLevel);
    payload->setProperty("outputGain", outputGain);
    payload->setProperty("eqBands", eqBands);
    payload->setProperty("reverbSize", reverbSize);
    payload->setProperty("reverbDamping", reverbDamping);
    payload->setProperty("reverbMix", reverbMix);
    payload->setProperty("reverbWidth", reverbWidth);
    payload->setProperty("eqBypassed", eqBypassed);
    payload->setProperty("overdriveBypassed", overdriveBypassed);
    payload->setProperty("driveBypassed", driveBypassed);
    payload->setProperty("fuzzBypassed", fuzzBypassed);
    payload->setProperty("synthFuzzBypassed", synthFuzzBypassed);
    payload->setProperty("compressorBypassed", compressorBypassed);
    payload->setProperty("octaveBypassed", octaveBypassed);
    payload->setProperty("doublerBypassed", doublerBypassed);
    payload->setProperty("tremoloBypassed", tremoloBypassed);
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

juce::WebBrowserComponent::Options makeWebViewOptions(ObstacleAudioProcessor& processor,
                                                      const juce::File& distRoot)
{
    auto input = processor.apvts.getRawParameterValue("Input Gain")->load();
    auto output = processor.apvts.getRawParameterValue("Output Gain")->load();
    auto monoInput = processor.apvts.getRawParameterValue(paramMonoInput)->load() > 0.5f;
    auto mute = processor.apvts.getRawParameterValue(paramMute)->load() > 0.5f;
    auto tunerReference = processor.apvts.getRawParameterValue("Tuner Reference")->load();
    auto gateThreshold = processor.apvts.getRawParameterValue("Gate Threshold")->load();
    auto compressorAmount = processor.apvts.getRawParameterValue("Compressor Amount")->load();
    auto compressorTone = processor.apvts.getRawParameterValue("Compressor Tone")->load();
    auto compressorLevel = processor.apvts.getRawParameterValue("Compressor Level")->load();
    auto octaveTranspose = processor.apvts.getRawParameterValue("Octave Transpose")->load();
    auto octaveMix = processor.apvts.getRawParameterValue("Octave Mix")->load();
    auto octaveTone = processor.apvts.getRawParameterValue("Octave Tone")->load();
    auto octaveMonoDetector = processor.apvts.getRawParameterValue("Octave Mono Detector")->load() > 0.5f;
    auto doublerMix = processor.apvts.getRawParameterValue("Doubler Mix")->load();
    auto doublerDelay = processor.apvts.getRawParameterValue("Doubler Delay")->load();
    auto doublerDetune = processor.apvts.getRawParameterValue("Doubler Detune")->load();
    auto tremoloSpeed = processor.apvts.getRawParameterValue("Tremolo Speed")->load();
    auto tremoloDepth = processor.apvts.getRawParameterValue("Tremolo Depth")->load();
    int tremoloLfoIndex = 0;
    if (auto* lfoParam = dynamic_cast<juce::AudioParameterChoice*>(
            processor.apvts.getParameter("Tremolo LFO")))
        tremoloLfoIndex = lfoParam->getIndex();
    auto tremoloStereoPhase = processor.apvts.getRawParameterValue("Tremolo Stereo Phase")->load() > 0.5f;
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
    auto overdriveDrive = processor.apvts.getRawParameterValue("Overdrive Drive")->load();
    auto overdriveTone = processor.apvts.getRawParameterValue("Overdrive Tone")->load();
    auto overdriveLevel = processor.apvts.getRawParameterValue("Overdrive Level")->load();
    auto fuzzDrive = processor.apvts.getRawParameterValue("Fuzz Drive")->load();
    auto fuzzTone = processor.apvts.getRawParameterValue("Fuzz Tone")->load();
    auto fuzzLevel = processor.apvts.getRawParameterValue("Fuzz Level")->load();
    auto synthFuzzMix = processor.apvts.getRawParameterValue("Synth Fuzz Mix")->load();
    auto synthFuzzDelay = processor.apvts.getRawParameterValue("Synth Fuzz Delay")->load();
    auto synthFuzzDetune = processor.apvts.getRawParameterValue("Synth Fuzz Detune")->load();
    auto synthFuzzDrive = processor.apvts.getRawParameterValue("Synth Fuzz Drive")->load();
    auto synthFuzzLevel = processor.apvts.getRawParameterValue("Synth Fuzz Level")->load();
    juce::Array<juce::var> eqBands;
    eqBands.ensureStorageAllocated(numEqBands);
    for (int i = 0; i < numEqBands; ++i)
        eqBands.add(processor.apvts.getRawParameterValue(paramEqBand(i))->load());
    auto eqBypassed = processor.apvts.getRawParameterValue(paramEqBypassed)->load() > 0.5f;
    auto overdriveBypassed = processor.apvts.getRawParameterValue("Overdrive Bypassed")->load() > 0.5f;
    auto driveBypassed = processor.apvts.getRawParameterValue("Distortion Bypassed")->load() > 0.5f;
    auto fuzzBypassed = processor.apvts.getRawParameterValue("Fuzz Bypassed")->load() > 0.5f;
    auto synthFuzzBypassed = processor.apvts.getRawParameterValue("Synth Fuzz Bypassed")->load() > 0.5f;
    auto compressorBypassed = processor.apvts.getRawParameterValue("Compressor Bypassed")->load() > 0.5f;
    auto octaveBypassed = processor.apvts.getRawParameterValue("Octave Bypassed")->load() > 0.5f;
    auto doublerBypassed = processor.apvts.getRawParameterValue("Doubler Bypassed")->load() > 0.5f;
    auto tremoloBypassed = processor.apvts.getRawParameterValue("Tremolo Bypassed")->load() > 0.5f;
    auto delayBypassed = processor.apvts.getRawParameterValue("Delay Bypassed")->load() > 0.5f;
    auto reverbSize = processor.apvts.getRawParameterValue("Reverb Size")->load();
    auto reverbDamping = processor.apvts.getRawParameterValue("Reverb Damping")->load();
    auto reverbMix = processor.apvts.getRawParameterValue("Reverb Mix")->load();
    auto reverbWidth = processor.apvts.getRawParameterValue("Reverb Width")->load();
    auto reverbBypassed = processor.apvts.getRawParameterValue("Reverb Bypassed")->load() > 0.5f;
    auto tunerBypassed = processor.apvts.getRawParameterValue("Tuner Bypassed")->load() > 0.5f;
    auto gateBypassed = processor.apvts.getRawParameterValue("Gate Bypassed")->load() > 0.5f;
    auto analyzerEnabled = processor.apvts.getRawParameterValue("Analyzer Enabled")->load() > 0.5f;

    juce::File userDataFolder;
   #if JUCE_WINDOWS
    userDataFolder = juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                         .getChildFile("Retrokielto")
                         .getChildFile("Obstacle")
                         .getChildFile("WebView2Cache");
    userDataFolder.createDirectory();
   #else
    userDataFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("ObstacleWebView2Data");
   #endif

    return juce::WebBrowserComponent::Options{}
        .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2{}
                                    .withUserDataFolder(userDataFolder)
                                    .withStatusBarDisabled()
                                    .withAdditionalBrowserArguments(juce::String::fromUTF8("--allow-no-sandbox-job --disable-gpu")))
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
                                    output,
                                    monoInput,
                                    mute,
                                    tunerReference,
                                    gateThreshold,
                                    compressorAmount,
                                    compressorTone,
                                    compressorLevel,
                                    octaveTranspose,
                                    octaveMix,
                                    octaveTone,
                                    octaveMonoDetector,
                                    doublerMix,
                                    doublerDelay,
                                    doublerDetune,
                                    tremoloSpeed,
                                    tremoloDepth,
                                    tremoloLfoIndex,
                                    tremoloStereoPhase,
                                    delayMix,
                                    delayTimeL,
                                    delayTimeR,
                                    delayFeedback,
                                    delayModeIsDual,
                                    overdriveDrive,
                                    overdriveTone,
                                    overdriveLevel,
                                    drive,
                                    distortionTone,
                                    distortionLevel,
                                    fuzzDrive,
                                    fuzzTone,
                                    fuzzLevel,
                                    synthFuzzMix,
                                    synthFuzzDelay,
                                    synthFuzzDetune,
                                    synthFuzzDrive,
                                    synthFuzzLevel,
                                    juce::var(eqBands),
                                    reverbSize,
                                    reverbDamping,
                                    reverbMix,
                                    reverbWidth,
                                    eqBypassed,
                                    overdriveBypassed,
                                    driveBypassed,
                                    fuzzBypassed,
                                    synthFuzzBypassed,
                                    compressorBypassed,
                                    octaveBypassed,
                                    doublerBypassed,
                                    tremoloBypassed,
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

ObstacleWebViewEditor::ObstacleWebViewEditor(ObstacleAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
    , webView(makeWebViewOptions(p, getWebUiDistRoot()))
{
    addAndMakeVisible(webView);

    fallbackLabel.setJustificationType(juce::Justification::centred);
    fallbackLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    fallbackLabel.setText("Obstacle Web UI not found. Run: npm install && npm run build in WebUI.",
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
    audioProcessor.apvts.addParameterListener(paramOctaveMix, this);
    audioProcessor.apvts.addParameterListener(paramOctaveTone, this);
    audioProcessor.apvts.addParameterListener(paramOctaveMonoDetector, this);
    audioProcessor.apvts.addParameterListener(paramDoublerMix, this);
    audioProcessor.apvts.addParameterListener(paramDoublerDelay, this);
    audioProcessor.apvts.addParameterListener(paramDoublerDetune, this);
    audioProcessor.apvts.addParameterListener(paramDoublerBypassed, this);
    audioProcessor.apvts.addParameterListener(paramTremoloSpeed, this);
    audioProcessor.apvts.addParameterListener(paramTremoloDepth, this);
    audioProcessor.apvts.addParameterListener(paramTremoloLfo, this);
    audioProcessor.apvts.addParameterListener(paramTremoloStereoPhase, this);
    audioProcessor.apvts.addParameterListener(paramTremoloBypassed, this);
    audioProcessor.apvts.addParameterListener(paramDelayMix, this);
    audioProcessor.apvts.addParameterListener(paramDelayTimeL, this);
    audioProcessor.apvts.addParameterListener(paramDelayTimeR, this);
    audioProcessor.apvts.addParameterListener(paramDelayFeedback, this);
    audioProcessor.apvts.addParameterListener(paramDelayMode, this);
    audioProcessor.apvts.addParameterListener(paramDelayBypassed, this);
    audioProcessor.apvts.addParameterListener(paramMonoInput, this);
    audioProcessor.apvts.addParameterListener(paramMute, this);
    audioProcessor.apvts.addParameterListener(paramOctaveBypassed, this);
    audioProcessor.apvts.addParameterListener(paramDrive, this);
    audioProcessor.apvts.addParameterListener(paramDistortionTone, this);
    audioProcessor.apvts.addParameterListener(paramDistortionLevel, this);
    audioProcessor.apvts.addParameterListener(paramOverdriveDrive, this);
    audioProcessor.apvts.addParameterListener(paramOverdriveTone, this);
    audioProcessor.apvts.addParameterListener(paramOverdriveLevel, this);
    audioProcessor.apvts.addParameterListener(paramFuzzDrive, this);
    audioProcessor.apvts.addParameterListener(paramFuzzTone, this);
    audioProcessor.apvts.addParameterListener(paramFuzzLevel, this);
    audioProcessor.apvts.addParameterListener(paramSynthFuzzMix, this);
    audioProcessor.apvts.addParameterListener(paramSynthFuzzDelay, this);
    audioProcessor.apvts.addParameterListener(paramSynthFuzzDetune, this);
    audioProcessor.apvts.addParameterListener(paramSynthFuzzDrive, this);
    audioProcessor.apvts.addParameterListener(paramSynthFuzzLevel, this);
    audioProcessor.apvts.addParameterListener(paramOutputGain, this);
    for (int i = 0; i < numEqBands; ++i)
        audioProcessor.apvts.addParameterListener(paramEqBand(i), this);
    audioProcessor.apvts.addParameterListener(paramEqBypassed, this);
    audioProcessor.apvts.addParameterListener(paramOverdriveBypassed, this);
    audioProcessor.apvts.addParameterListener(paramDriveBypassed, this);
    audioProcessor.apvts.addParameterListener(paramFuzzBypassed, this);
    audioProcessor.apvts.addParameterListener(paramSynthFuzzBypassed, this);
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

ObstacleWebViewEditor::~ObstacleWebViewEditor()
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
    audioProcessor.apvts.removeParameterListener(paramOctaveMix, this);
    audioProcessor.apvts.removeParameterListener(paramOctaveTone, this);
    audioProcessor.apvts.removeParameterListener(paramOctaveMonoDetector, this);
    audioProcessor.apvts.removeParameterListener(paramDoublerMix, this);
    audioProcessor.apvts.removeParameterListener(paramDoublerDelay, this);
    audioProcessor.apvts.removeParameterListener(paramDoublerDetune, this);
    audioProcessor.apvts.removeParameterListener(paramDoublerBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramTremoloSpeed, this);
    audioProcessor.apvts.removeParameterListener(paramTremoloDepth, this);
    audioProcessor.apvts.removeParameterListener(paramTremoloLfo, this);
    audioProcessor.apvts.removeParameterListener(paramTremoloStereoPhase, this);
    audioProcessor.apvts.removeParameterListener(paramTremoloBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramDelayMix, this);
    audioProcessor.apvts.removeParameterListener(paramDelayTimeL, this);
    audioProcessor.apvts.removeParameterListener(paramDelayTimeR, this);
    audioProcessor.apvts.removeParameterListener(paramDelayFeedback, this);
    audioProcessor.apvts.removeParameterListener(paramDelayMode, this);
    audioProcessor.apvts.removeParameterListener(paramDelayBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramMonoInput, this);
    audioProcessor.apvts.removeParameterListener(paramMute, this);
    audioProcessor.apvts.removeParameterListener(paramOctaveBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramDrive, this);
    audioProcessor.apvts.removeParameterListener(paramDistortionTone, this);
    audioProcessor.apvts.removeParameterListener(paramDistortionLevel, this);
    audioProcessor.apvts.removeParameterListener(paramOverdriveDrive, this);
    audioProcessor.apvts.removeParameterListener(paramOverdriveTone, this);
    audioProcessor.apvts.removeParameterListener(paramOverdriveLevel, this);
    audioProcessor.apvts.removeParameterListener(paramFuzzDrive, this);
    audioProcessor.apvts.removeParameterListener(paramFuzzTone, this);
    audioProcessor.apvts.removeParameterListener(paramFuzzLevel, this);
    audioProcessor.apvts.removeParameterListener(paramSynthFuzzMix, this);
    audioProcessor.apvts.removeParameterListener(paramSynthFuzzDelay, this);
    audioProcessor.apvts.removeParameterListener(paramSynthFuzzDetune, this);
    audioProcessor.apvts.removeParameterListener(paramSynthFuzzDrive, this);
    audioProcessor.apvts.removeParameterListener(paramSynthFuzzLevel, this);
    audioProcessor.apvts.removeParameterListener(paramOutputGain, this);
    for (int i = 0; i < numEqBands; ++i)
        audioProcessor.apvts.removeParameterListener(paramEqBand(i), this);
    audioProcessor.apvts.removeParameterListener(paramEqBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramOverdriveBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramDriveBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramFuzzBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramSynthFuzzBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramCompressorBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramReverbSize, this);
    audioProcessor.apvts.removeParameterListener(paramReverbDamping, this);
    audioProcessor.apvts.removeParameterListener(paramReverbMix, this);
    audioProcessor.apvts.removeParameterListener(paramReverbWidth, this);
    audioProcessor.apvts.removeParameterListener(paramReverbBypassed, this);
    audioProcessor.apvts.removeParameterListener(paramAnalyzerEnabled, this);
}

void ObstacleWebViewEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void ObstacleWebViewEditor::resized()
{
    webView.setBounds(getLocalBounds());
    fallbackLabel.setBounds(getLocalBounds().reduced(20));
}

juce::String ObstacleWebViewEditor::resolveWebUiUrl() const
{
    auto overrideUrl = juce::SystemStats::getEnvironmentVariable("SIMPLEEQ_WEB_UI_URL", {});
    if (overrideUrl.isNotEmpty())
        return overrideUrl;

    auto distRoot = getWebUiDistRoot();
    if (distRoot.isDirectory())
        return juce::WebBrowserComponent::getResourceProviderRoot();

    return {};
}

juce::File ObstacleWebViewEditor::getWebUiDistRoot() const
{
    auto localIndex = findLocalWebUiDistIndex();
    if (localIndex.existsAsFile())
        return localIndex.getParentDirectory();

    return {};
}

void ObstacleWebViewEditor::parameterChanged(const juce::String& parameterID, float newValue)
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
        && parameterID != paramOctaveMix
        && parameterID != paramOctaveTone
        && parameterID != paramOctaveMonoDetector
        && parameterID != paramDoublerMix
        && parameterID != paramDoublerDelay
        && parameterID != paramDoublerDetune
        && parameterID != paramDoublerBypassed
        && parameterID != paramTremoloSpeed
        && parameterID != paramTremoloDepth
        && parameterID != paramTremoloLfo
        && parameterID != paramTremoloStereoPhase
        && parameterID != paramTremoloBypassed
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
        && parameterID != paramOverdriveDrive
        && parameterID != paramOverdriveTone
        && parameterID != paramOverdriveLevel
        && parameterID != paramFuzzDrive
        && parameterID != paramFuzzTone
        && parameterID != paramFuzzLevel
        && parameterID != paramSynthFuzzMix
        && parameterID != paramSynthFuzzDelay
        && parameterID != paramSynthFuzzDetune
        && parameterID != paramSynthFuzzDrive
        && parameterID != paramSynthFuzzLevel
        && parameterID != paramOutputGain
        && parameterID != paramMonoInput
        && parameterID != paramMute
        && parameterID != paramEqBypassed
        && parameterID != paramEqBand(0)
        && parameterID != paramEqBand(1)
        && parameterID != paramEqBand(2)
        && parameterID != paramEqBand(3)
        && parameterID != paramEqBand(4)
        && parameterID != paramEqBand(5)
        && parameterID != paramEqBand(6)
        && parameterID != paramEqBand(7)
        && parameterID != paramEqBand(8)
        && parameterID != paramEqBand(9)
        && parameterID != paramOverdriveBypassed
        && parameterID != paramDriveBypassed
        && parameterID != paramFuzzBypassed
        && parameterID != paramSynthFuzzBypassed
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

void ObstacleWebViewEditor::handleAsyncUpdate()
{
    if (!parameterUpdatePending.exchange(false, std::memory_order_acq_rel))
        return;

    emitParameterSnapshotToFrontend();
}

void ObstacleWebViewEditor::timerCallback()
{
    emitParameterSnapshotToFrontend();
}

void ObstacleWebViewEditor::emitParameterSnapshotToFrontend()
{
    webView.emitEventIfBrowserIsVisible(juce::Identifier(backendParametersEvent), makeParameterSnapshot());
}

juce::var ObstacleWebViewEditor::makeParameterSnapshot()
{
    auto input = audioProcessor.apvts.getRawParameterValue(paramInputGain)->load();
    auto output = audioProcessor.apvts.getRawParameterValue(paramOutputGain)->load();
    auto monoInput = audioProcessor.apvts.getRawParameterValue(paramMonoInput)->load() > 0.5f;
    auto mute = audioProcessor.apvts.getRawParameterValue(paramMute)->load() > 0.5f;
    auto tunerReference = audioProcessor.apvts.getRawParameterValue(paramTunerReference)->load();
    auto gateThreshold = audioProcessor.apvts.getRawParameterValue(paramGateThreshold)->load();
    auto compressorAmount = audioProcessor.apvts.getRawParameterValue(paramCompressorAmount)->load();
    auto compressorTone = audioProcessor.apvts.getRawParameterValue(paramCompressorTone)->load();
    auto compressorLevel = audioProcessor.apvts.getRawParameterValue(paramCompressorLevel)->load();
    auto octaveTranspose = audioProcessor.apvts.getRawParameterValue(paramOctaveTranspose)->load();
    auto octaveMix = audioProcessor.apvts.getRawParameterValue(paramOctaveMix)->load();
    auto octaveTone = audioProcessor.apvts.getRawParameterValue(paramOctaveTone)->load();
    auto octaveMonoDetector = audioProcessor.apvts.getRawParameterValue(paramOctaveMonoDetector)->load() > 0.5f;
    auto doublerMix = audioProcessor.apvts.getRawParameterValue(paramDoublerMix)->load();
    auto doublerDelay = audioProcessor.apvts.getRawParameterValue(paramDoublerDelay)->load();
    auto doublerDetune = audioProcessor.apvts.getRawParameterValue(paramDoublerDetune)->load();
    auto tremoloSpeed = audioProcessor.apvts.getRawParameterValue(paramTremoloSpeed)->load();
    auto tremoloDepth = audioProcessor.apvts.getRawParameterValue(paramTremoloDepth)->load();
    int tremoloLfoIndex = 0;
    if (auto* lfoParam = dynamic_cast<juce::AudioParameterChoice*>(
            audioProcessor.apvts.getParameter(paramTremoloLfo)))
        tremoloLfoIndex = lfoParam->getIndex();
    auto tremoloStereoPhase = audioProcessor.apvts.getRawParameterValue(paramTremoloStereoPhase)->load() > 0.5f;
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
    auto overdriveDrive = audioProcessor.apvts.getRawParameterValue(paramOverdriveDrive)->load();
    auto overdriveTone = audioProcessor.apvts.getRawParameterValue(paramOverdriveTone)->load();
    auto overdriveLevel = audioProcessor.apvts.getRawParameterValue(paramOverdriveLevel)->load();
    auto fuzzDrive = audioProcessor.apvts.getRawParameterValue(paramFuzzDrive)->load();
    auto fuzzTone = audioProcessor.apvts.getRawParameterValue(paramFuzzTone)->load();
    auto fuzzLevel = audioProcessor.apvts.getRawParameterValue(paramFuzzLevel)->load();
    auto synthFuzzMix = audioProcessor.apvts.getRawParameterValue(paramSynthFuzzMix)->load();
    auto synthFuzzDelay = audioProcessor.apvts.getRawParameterValue(paramSynthFuzzDelay)->load();
    auto synthFuzzDetune = audioProcessor.apvts.getRawParameterValue(paramSynthFuzzDetune)->load();
    auto synthFuzzDrive = audioProcessor.apvts.getRawParameterValue(paramSynthFuzzDrive)->load();
    auto synthFuzzLevel = audioProcessor.apvts.getRawParameterValue(paramSynthFuzzLevel)->load();
    juce::Array<juce::var> eqBands;
    eqBands.ensureStorageAllocated(numEqBands);
    for (int i = 0; i < numEqBands; ++i)
        eqBands.add(audioProcessor.apvts.getRawParameterValue(paramEqBand(i))->load());
    auto eqBypassed = audioProcessor.apvts.getRawParameterValue(paramEqBypassed)->load() > 0.5f;
    auto overdriveBypassed = audioProcessor.apvts.getRawParameterValue(paramOverdriveBypassed)->load() > 0.5f;
    auto driveBypassed = audioProcessor.apvts.getRawParameterValue(paramDriveBypassed)->load() > 0.5f;
    auto fuzzBypassed = audioProcessor.apvts.getRawParameterValue(paramFuzzBypassed)->load() > 0.5f;
    auto synthFuzzBypassed = audioProcessor.apvts.getRawParameterValue(paramSynthFuzzBypassed)->load() > 0.5f;
    auto compressorBypassed = audioProcessor.apvts.getRawParameterValue(paramCompressorBypassed)->load() > 0.5f;
    auto octaveBypassed = audioProcessor.apvts.getRawParameterValue(paramOctaveBypassed)->load() > 0.5f;
    auto doublerBypassed = audioProcessor.apvts.getRawParameterValue(paramDoublerBypassed)->load() > 0.5f;
    auto tremoloBypassed = audioProcessor.apvts.getRawParameterValue(paramTremoloBypassed)->load() > 0.5f;
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
                           output,
                           monoInput,
                           mute,
                           tunerReference,
                           gateThreshold,
                           compressorAmount,
                           compressorTone,
                           compressorLevel,
                           octaveTranspose,
                           octaveMix,
                           octaveTone,
                           octaveMonoDetector,
                           doublerMix,
                           doublerDelay,
                           doublerDetune,
                           tremoloSpeed,
                           tremoloDepth,
                           tremoloLfoIndex,
                           tremoloStereoPhase,
                           delayMix,
                           delayTimeL,
                           delayTimeR,
                           delayFeedback,
                             delayModeIsDual,
                             overdriveDrive,
                             overdriveTone,
                             overdriveLevel,
                             drive,
                            distortionTone,
                            distortionLevel,
                             fuzzDrive,
                             fuzzTone,
                             fuzzLevel,
                             synthFuzzMix,
                             synthFuzzDelay,
                             synthFuzzDetune,
                             synthFuzzDrive,
                             synthFuzzLevel,
                             juce::var(eqBands),
                            reverbSize,
                            reverbDamping,
                            reverbMix,
                            reverbWidth,
                            eqBypassed,
                            overdriveBypassed,
                             driveBypassed,
                            fuzzBypassed,
                            synthFuzzBypassed,
                            compressorBypassed,
                           octaveBypassed,
                           doublerBypassed,
                           tremoloBypassed,
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

juce::var ObstacleWebViewEditor::makeFloatArrayVar(const std::vector<float>& values)
{
    juce::Array<juce::var> array;
    array.ensureStorageAllocated(static_cast<int>(values.size()));
    for (auto value : values)
        array.add(value);

    return juce::var(array);
}

std::vector<float> ObstacleWebViewEditor::buildSpectrumFromFifo(SingleChannelSampleFifo<ObstacleAudioProcessor::BlockType>& fifo,
                                                                 juce::AudioBuffer<float>& monoBuffer,
                                                                 std::vector<float>& fftScratch,
                                                                 std::vector<float>& fftResult)
{
    ObstacleAudioProcessor::BlockType tempIncomingBuffer;
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

std::vector<float> ObstacleWebViewEditor::buildResponseCurve() const
{
    auto settings = getChainSettings(audioProcessor.apvts);
    const auto sampleRate = juce::jmax(1.0, audioProcessor.getSampleRate());

    static constexpr std::array<float, 10> eqFrequencies {
        31.25f, 62.5f, 125.0f, 250.0f, 500.0f,
        1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };
    static constexpr float eqQ = 1.41421356f;

    std::vector<juce::dsp::IIR::Coefficients<float>::Ptr> bandCoeffs;
    bandCoeffs.reserve(eqFrequencies.size());
    if (!settings.graphicEqBypassed)
    {
        for (size_t i = 0; i < eqFrequencies.size(); ++i)
        {
            const auto gainDb = settings.graphicEqBandDb[i];
            bandCoeffs.push_back(juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                sampleRate, eqFrequencies[i], eqQ,
                juce::Decibels::decibelsToGain(gainDb)));
        }
    }

    std::vector<float> magnitudes(static_cast<size_t>(spectrumPoints), 0.0f);

    for (int i = 0; i < spectrumPoints; ++i)
    {
        const auto normalizedX = spectrumPoints == 1
            ? 0.0f
            : static_cast<float>(i) / static_cast<float>(spectrumPoints - 1);
        const auto frequency = juce::mapToLog10(normalizedX, 20.0f, 20000.0f);

        double mag = 1.0;
        for (const auto& coeffs : bandCoeffs)
            mag *= coeffs->getMagnitudeForFrequency(frequency, sampleRate);

        magnitudes[static_cast<size_t>(i)] = juce::Decibels::gainToDecibels(static_cast<float>(mag), -24.0f);
    }

    return magnitudes;
}
