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
                          float compressorAmount,
                          float drive,
                          float outputGain,
                          float lowCutFreq,
                          float highCutFreq,
                          float peakFreq,
                          float peakGain,
                          float peakQuality,
                          float lowCutSlope,
                          float highCutSlope,
                          bool lowCutBypassed,
                          bool peakBypassed,
                          bool highCutBypassed,
                          bool driveBypassed,
                          bool compressorBypassed,
                          float inputPeak,
                          float outputPeak,
                          bool inputClipping,
                          bool outputClipping,
                          juce::var responseCurve,
                          juce::var leftSpectrum,
                          juce::var rightSpectrum)
{
    auto payload = std::make_unique<juce::DynamicObject>();
    payload->setProperty("inputGain", inputGain);
    payload->setProperty("compressorAmount", compressorAmount);
    payload->setProperty("drive", drive);
    payload->setProperty("outputGain", outputGain);
    payload->setProperty("lowCutFreq", lowCutFreq);
    payload->setProperty("highCutFreq", highCutFreq);
    payload->setProperty("peakFreq", peakFreq);
    payload->setProperty("peakGain", peakGain);
    payload->setProperty("peakQuality", peakQuality);
    payload->setProperty("lowCutSlope", lowCutSlope);
    payload->setProperty("highCutSlope", highCutSlope);
    payload->setProperty("lowCutBypassed", lowCutBypassed);
    payload->setProperty("peakBypassed", peakBypassed);
    payload->setProperty("highCutBypassed", highCutBypassed);
    payload->setProperty("driveBypassed", driveBypassed);
    payload->setProperty("compressorBypassed", compressorBypassed);
    payload->setProperty("inputPeak", inputPeak);
    payload->setProperty("outputPeak", outputPeak);
    payload->setProperty("inputClipping", inputClipping);
    payload->setProperty("outputClipping", outputClipping);
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
    auto compressorAmount = processor.apvts.getRawParameterValue("Compressor Amount")->load();
    auto drive = processor.apvts.getRawParameterValue("Distortion Drive")->load();
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
    auto compressorBypassed = processor.apvts.getRawParameterValue("Compressor Bypassed")->load() > 0.5f;

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
        })
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
                                    compressorAmount,
                                    drive,
                                    output,
                                    lowCutFreq,
                                    highCutFreq,
                                    peakFreq,
                                    peakGain,
                                    peakQuality,
                                    lowCutSlope,
                                    highCutSlope,
                                    lowCutBypassed,
                                    peakBypassed,
                                    highCutBypassed,
                                    driveBypassed,
                                    compressorBypassed,
                                    0.0f,
                                    0.0f,
                                    false,
                                    false,
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
    audioProcessor.apvts.addParameterListener(paramCompressorAmount, this);
    audioProcessor.apvts.addParameterListener(paramDrive, this);
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
    audioProcessor.apvts.addParameterListener(paramCompressorBypassed, this);

    startTimerHz(24);

    setSize(960, 640);
}

SimpleEQWebViewEditor::~SimpleEQWebViewEditor()
{
    cancelPendingUpdate();
    stopTimer();
    audioProcessor.apvts.removeParameterListener(paramInputGain, this);
    audioProcessor.apvts.removeParameterListener(paramCompressorAmount, this);
    audioProcessor.apvts.removeParameterListener(paramDrive, this);
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
    audioProcessor.apvts.removeParameterListener(paramCompressorBypassed, this);
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
        && parameterID != paramCompressorAmount
        && parameterID != paramDrive
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
        && parameterID != paramCompressorBypassed)
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
    auto compressorAmount = audioProcessor.apvts.getRawParameterValue(paramCompressorAmount)->load();
    auto drive = audioProcessor.apvts.getRawParameterValue(paramDrive)->load();
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
    auto compressorBypassed = audioProcessor.apvts.getRawParameterValue(paramCompressorBypassed)->load() > 0.5f;
    auto inputPeak = audioProcessor.consumeInputPeakLevel();
    auto outputPeak = audioProcessor.consumeOutputPeakLevel();
    auto inputClipping = audioProcessor.consumeInputClippingFlag();
    auto outputClipping = audioProcessor.consumeOutputClippingFlag();
    auto responseCurve = buildResponseCurve();
    auto leftSpectrum = buildSpectrumFromFifo(audioProcessor.leftChannelFifo, leftFftScratch, leftFftResult);
    auto rightSpectrum = buildSpectrumFromFifo(audioProcessor.rightChannelFifo, rightFftScratch, rightFftResult);

    return makeSnapshotVar(input,
                           compressorAmount,
                           drive,
                           output,
                           lowCutFreq,
                           highCutFreq,
                           peakFreq,
                           peakGain,
                           peakQuality,
                           lowCutSlope,
                           highCutSlope,
                           lowCutBypassed,
                           peakBypassed,
                           highCutBypassed,
                           driveBypassed,
                           compressorBypassed,
                           inputPeak,
                           outputPeak,
                           inputClipping,
                           outputClipping,
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
                                                                 std::vector<float>& fftScratch,
                                                                 std::vector<float>& fftResult)
{
    SimpleEQAudioProcessor::BlockType buffer;
    bool hasData = false;

    while (fifo.getNumCompleteBuffersAvailable() > 0)
    {
        if (fifo.getAudioBuffer(buffer))
            hasData = true;
    }

    if (!hasData)
        return fftResult;

    std::fill(fftScratch.begin(), fftScratch.end(), 0.0f);

    const auto sampleCount = juce::jmin(fftSize, buffer.getNumSamples());
    const auto* input = buffer.getReadPointer(0);
    std::copy(input, input + sampleCount, fftScratch.begin());

    analyzerWindow.multiplyWithWindowingTable(fftScratch.data(), fftSize);
    analyzerFft.performFrequencyOnlyForwardTransform(fftScratch.data());

    const auto sampleRate = juce::jmax(1.0, audioProcessor.getSampleRate());
    const auto numBins = fftSize / 2;

    constexpr float releaseAlpha = 0.45f;
    constexpr float bandHalfWidth = 0.3f;

    for (int i = 0; i < spectrumPoints; ++i)
    {
        const auto denom = spectrumPoints == 1 ? 1.0f : static_cast<float>(spectrumPoints - 1);
        const auto leftNorm = juce::jlimit(0.0f, 1.0f, (static_cast<float>(i) - bandHalfWidth) / denom);
        const auto rightNorm = juce::jlimit(0.0f, 1.0f, (static_cast<float>(i) + bandHalfWidth) / denom);

        const auto leftFrequency = juce::mapToLog10(leftNorm, 20.0f, 20000.0f);
        const auto rightFrequency = juce::mapToLog10(rightNorm, 20.0f, 20000.0f);

        const auto binStart = juce::jlimit(0,
                                           numBins - 1,
                                           static_cast<int>(std::floor(leftFrequency * fftSize / sampleRate)));
        const auto binEnd = juce::jlimit(0,
                                         numBins - 1,
                                         static_cast<int>(std::ceil(rightFrequency * fftSize / sampleRate)));

        auto maxBinMagnitude = 0.0f;
        for (int bin = binStart; bin <= juce::jmax(binStart, binEnd); ++bin)
            maxBinMagnitude = juce::jmax(maxBinMagnitude, fftScratch[bin]);

        auto magnitude = maxBinMagnitude / static_cast<float>(numBins);
        if (!std::isfinite(magnitude) || magnitude < 0.0f)
            magnitude = 0.0f;

        const auto targetDb = juce::Decibels::gainToDecibels(magnitude, -120.0f);
        auto& currentDb = fftResult[static_cast<size_t>(i)];

        // Instant rise with slower fall keeps harmonic spikes sharp and readable.
        if (targetDb >= currentDb)
            currentDb = targetDb;
        else
            currentDb = releaseAlpha * currentDb + (1.0f - releaseAlpha) * targetDb;
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
