#include "WebViewEditor.h"

namespace
{
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

juce::WebBrowserComponent::Options makeWebViewOptions(BluePrinterAudioProcessor& processor,
                                                      const juce::File& distRoot,
                                                      const juce::var& initialData)
{
    juce::File userDataFolder;
   #if JUCE_WINDOWS
    userDataFolder = juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                         .getChildFile("Retrokielto")
                         .getChildFile("BluePrinter")
                         .getChildFile("WebView2Cache");
    userDataFolder.createDirectory();
   #else
    userDataFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("BluePrinterWebView2Data");
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
        .withEventListener(BluePrinterWebViewEditor::frontendSetParameterEvent, [&processor](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const auto parameterID = obj->getProperty("id").toString();
                const auto value = static_cast<float>(obj->getProperty("value"));

                if (auto* parameter = processor.apvts.getParameter(parameterID))
                    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            }
        })
        .withInitialisationData("parameters", initialData);
}
}

BluePrinterWebViewEditor::BluePrinterWebViewEditor(BluePrinterAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
    , webView(makeWebViewOptions(p, getWebUiDistRoot(), makeParameterSnapshot()))
{
    addAndMakeVisible(webView);

    fallbackLabel.setJustificationType(juce::Justification::centred);
    fallbackLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    fallbackLabel.setText("BluePrinter Web UI not found. Run: npm install && npm run build in WebUI.",
                          juce::dontSendNotification);
    addAndMakeVisible(fallbackLabel);

    if (!juce::WebBrowserComponent::areOptionsSupported(makeWebViewOptions(p, getWebUiDistRoot(), makeParameterSnapshot())))
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

    audioProcessor.apvts.addParameterListener(paramGain, this);

    startTimerHz(60);

    setSize(960, 640);
}

BluePrinterWebViewEditor::~BluePrinterWebViewEditor()
{
    cancelPendingUpdate();
    stopTimer();
    audioProcessor.apvts.removeParameterListener(paramGain, this);
}

void BluePrinterWebViewEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void BluePrinterWebViewEditor::resized()
{
    webView.setBounds(getLocalBounds());
    fallbackLabel.setBounds(getLocalBounds().reduced(20));
}

juce::String BluePrinterWebViewEditor::resolveWebUiUrl() const
{
    auto overrideUrl = juce::SystemStats::getEnvironmentVariable("BLUEPRINTER_WEB_UI_URL", {});
    if (overrideUrl.isNotEmpty())
        return overrideUrl;

    auto distRoot = getWebUiDistRoot();
    if (distRoot.isDirectory())
        return juce::WebBrowserComponent::getResourceProviderRoot();

    return {};
}

juce::File BluePrinterWebViewEditor::getWebUiDistRoot() const
{
    auto localIndex = findLocalWebUiDistIndex();
    if (localIndex.existsAsFile())
        return localIndex.getParentDirectory();

    return {};
}

void BluePrinterWebViewEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);

    if (parameterID != paramGain)
        return;

    parameterUpdatePending.store(true, std::memory_order_release);
    triggerAsyncUpdate();
}

void BluePrinterWebViewEditor::handleAsyncUpdate()
{
    if (!parameterUpdatePending.exchange(false, std::memory_order_acq_rel))
        return;

    emitParameterSnapshotToFrontend();
}

void BluePrinterWebViewEditor::timerCallback()
{
    emitParameterSnapshotToFrontend();
}

void BluePrinterWebViewEditor::emitParameterSnapshotToFrontend()
{
    webView.emitEventIfBrowserIsVisible(juce::Identifier(backendParametersEvent), makeParameterSnapshot());
}

juce::var BluePrinterWebViewEditor::makeParameterSnapshot() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("gain", audioProcessor.apvts.getRawParameterValue(paramGain)->load());
    return juce::var(obj);
}
