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

juce::var snippetToVar (const Snippet& s)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("id", s.id);
    obj->setProperty ("name", s.name);
    obj->setProperty ("comments", s.comments);
    obj->setProperty ("sampleRate", s.sampleRate);
    obj->setProperty ("numChannels", s.numChannels);
    obj->setProperty ("numSamples", static_cast<double> (s.numSamples));
    obj->setProperty ("durationSeconds", s.sampleRate > 0.0 ? s.numSamples / s.sampleRate : 0.0);
    obj->setProperty ("createdAt", s.creationTime.toISO8601 (true));
    obj->setProperty ("savedPath", s.savedPath);

    auto* peaks = new juce::DynamicObject();
    juce::Array<juce::var> peakArray;
    peakArray.ensureStorageAllocated (static_cast<int> (s.peaks.size()));
    for (float p : s.peaks)
        peakArray.add (juce::var (p));
    obj->setProperty ("peaks", juce::var (peakArray));

    return juce::var (obj);
}
}

juce::WebBrowserComponent::Options makeWebViewOptions(BluePrinterAudioProcessor& processor,
                                                      const juce::File& distRoot,
                                                      BluePrinterWebViewEditor* owner,
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
        .withEventListener(BluePrinterWebViewEditor::frontendSetParameterEvent, [&processor, owner](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const auto parameterID = obj->getProperty("id").toString();
                const auto value = static_cast<float>(obj->getProperty("value"));

                if (auto* parameter = processor.apvts.getParameter(parameterID))
                    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            }
            juce::ignoreUnused (owner);
        })
        .withEventListener(BluePrinterWebViewEditor::frontendStartRecordingEvent, [&processor](juce::var)
        {
            processor.startRecording();
        })
        .withEventListener(BluePrinterWebViewEditor::frontendStopRecordingEvent, [&processor](juce::var)
        {
            processor.stopRecording();
        })
        .withEventListener(BluePrinterWebViewEditor::frontendStartPlaybackEvent, [&processor](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
                processor.startPlayback (static_cast<int> (obj->getProperty("id")));
        })
        .withEventListener(BluePrinterWebViewEditor::frontendStopPlaybackEvent, [&processor](juce::var)
        {
            processor.stopPlayback();
        })
        .withEventListener(BluePrinterWebViewEditor::frontendUpdateSnippetEvent, [&processor](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const int id = static_cast<int> (obj->getProperty("id"));
                processor.updateSnippetMeta (id, obj->getProperty("name").toString(), obj->getProperty("comments").toString());
            }
        })
        .withEventListener(BluePrinterWebViewEditor::frontendDeleteSnippetEvent, [&processor, owner](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const int id = static_cast<int> (obj->getProperty("id"));
                processor.deleteSnippet (id);
            }
            juce::ignoreUnused (owner);
        })
        .withEventListener(BluePrinterWebViewEditor::frontendSaveSnippetEvent, [owner](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const int id = static_cast<int> (obj->getProperty("id"));
                if (owner != nullptr)
                    owner->handleSaveSnippet (data);
                juce::ignoreUnused (id);
            }
        })
        .withEventListener(BluePrinterWebViewEditor::frontendRevealSnippetEvent, [owner](juce::var data)
        {
            if (owner != nullptr)
                owner->handleRevealSnippet (data);
        })
        .withEventListener(BluePrinterWebViewEditor::frontendChooseFolderEvent, [owner](juce::var)
        {
            if (owner != nullptr)
                owner->handleChooseLibraryFolder();
        })
        .withEventListener(BluePrinterWebViewEditor::frontendOpenFolderEvent, [&processor](juce::var)
        {
            auto folder = juce::File (processor.getLibraryFolder());
            if (folder.isDirectory())
                folder.startAsProcess();
        })
        .withInitialisationData("parameters", initialData)
        .withInitialisationData("snippets", owner->makeSnippetsSnapshot())
        .withInitialisationData("transport", owner->makeTransportSnapshot());
}

BluePrinterWebViewEditor::BluePrinterWebViewEditor(BluePrinterAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
    , webView(makeWebViewOptions(p, getWebUiDistRoot(), this, makeParameterSnapshot()))
{
    addAndMakeVisible(webView);

    fallbackLabel.setJustificationType(juce::Justification::centred);
    fallbackLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    fallbackLabel.setText("BluePrinter Web UI not found. Run: npm install && npm run build in WebUI.",
                          juce::dontSendNotification);
    addAndMakeVisible(fallbackLabel);

    if (!juce::WebBrowserComponent::areOptionsSupported(makeWebViewOptions(p, getWebUiDistRoot(), this, makeParameterSnapshot())))
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
    audioProcessor.addListener (this);

    startTimerHz (audioProcessor.transportTimerHz);

    setSize(960, 700);
}

BluePrinterWebViewEditor::~BluePrinterWebViewEditor()
{
    cancelPendingUpdate();
    stopTimer();
    audioProcessor.apvts.removeParameterListener(paramGain, this);
    audioProcessor.removeListener (this);
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
    if (parameterUpdatePending.exchange(false, std::memory_order_acq_rel))
        emitParameterSnapshotToFrontend();
    if (libraryUpdatePending.exchange(false, std::memory_order_acq_rel))
        emitLibraryToFrontend();
    if (transportUpdatePending.exchange(false, std::memory_order_acq_rel))
        emitTransportToFrontend();
}

void BluePrinterWebViewEditor::timerCallback()
{
    // Throttled push: always push transport so the meter / timecode moves.
    emitTransportToFrontend();
}

void BluePrinterWebViewEditor::libraryChanged()
{
    libraryUpdatePending.store(true, std::memory_order_release);
    triggerAsyncUpdate();
}

void BluePrinterWebViewEditor::transportChanged()
{
    transportUpdatePending.store(true, std::memory_order_release);
    triggerAsyncUpdate();
}

void BluePrinterWebViewEditor::emitParameterSnapshotToFrontend()
{
    webView.emitEventIfBrowserIsVisible(juce::Identifier(backendParametersEvent), makeParameterSnapshot());
}

void BluePrinterWebViewEditor::emitTransportToFrontend()
{
    webView.emitEventIfBrowserIsVisible(juce::Identifier(backendTransportEvent), makeTransportSnapshot());
}

void BluePrinterWebViewEditor::emitLibraryToFrontend()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("libraryFolder", audioProcessor.getLibraryFolder());
    obj->setProperty ("lastSaveError", audioProcessor.getLastSaveError());
    obj->setProperty ("snippets", makeSnippetsSnapshot());
    webView.emitEventIfBrowserIsVisible(juce::Identifier(backendSnippetsEvent), juce::var (obj));
}

juce::var BluePrinterWebViewEditor::makeParameterSnapshot() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("gain", audioProcessor.apvts.getRawParameterValue(paramGain)->load());
    return juce::var(obj);
}

juce::var BluePrinterWebViewEditor::makeSnippetsSnapshot() const
{
    auto snippets = audioProcessor.getLibrary().snapshot();
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated (static_cast<int> (snippets.size()));
    for (auto& s : snippets)
        arr.add (snippetToVar (*s));
    return juce::var (arr);
}

juce::var BluePrinterWebViewEditor::makeTransportSnapshot() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("recording", audioProcessor.isRecordingRequested());
    obj->setProperty ("recordingLength", audioProcessor.getRecordingLengthSamples());
    obj->setProperty ("recordingSampleRate", audioProcessor.getSampleRate());
    obj->setProperty ("playingSnippetId", audioProcessor.getPlayingSnippetId());
    obj->setProperty ("playingPosition", audioProcessor.getPlaybackPositionSamples());
    obj->setProperty ("inputLevel", juce::jlimit (0.0f, 1.0f, audioProcessor.getCurrentInputLevel()));
    obj->setProperty ("inputPeak",  juce::jlimit (0.0f, 1.0f, audioProcessor.getCurrentInputPeak()));
    obj->setProperty ("libraryFolder", audioProcessor.getLibraryFolder());
    obj->setProperty ("lastSaveError", audioProcessor.getLastSaveError());
    return juce::var (obj);
}

void BluePrinterWebViewEditor::handleSaveSnippet(const juce::var& data)
{
    if (auto* obj = data.getDynamicObject())
    {
        const int id = static_cast<int> (obj->getProperty("id"));
        juce::String startingFolder = obj->getProperty("folder").toString();
        juce::File folder (startingFolder);
        if (! folder.isDirectory())
            folder = juce::File (audioProcessor.getLibraryFolder());

        if (folder.isDirectory())
            saveSnippetWithDialog (id, folder);
        else
            pickLibraryFolderThenSave (id);
    }
}

void BluePrinterWebViewEditor::handleRevealSnippet(const juce::var& data)
{
    if (auto* obj = data.getDynamicObject())
    {
        const int id = static_cast<int> (obj->getProperty("id"));
        auto snippet = audioProcessor.getLibrary().findById (id);
        if (snippet == nullptr)
            return;
        juce::File target;
        if (snippet->savedPath.isNotEmpty())
            target = juce::File (snippet->savedPath);
        else
            target = juce::File (audioProcessor.getLibraryFolder());
        if (target.exists())
            target.revealToUser();
    }
}

void BluePrinterWebViewEditor::handleChooseLibraryFolder()
{
    juce::File start (audioProcessor.getLibraryFolder());
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    pickLibraryFolder (start);
}

void BluePrinterWebViewEditor::handleOpenLibraryFolder()
{
    auto folder = juce::File (audioProcessor.getLibraryFolder());
    if (folder.isDirectory())
        folder.startAsProcess();
}

void BluePrinterWebViewEditor::saveSnippetWithDialog(int snippetId, const juce::File& startingFolder)
{
    auto snippet = audioProcessor.getLibrary().findById (snippetId);
    if (snippet == nullptr)
    {
        sendNotification ("Snippet no longer exists.", "error");
        return;
    }

    auto defaultName = snippet->name.isNotEmpty() ? snippet->name : ("Snippet-" + juce::String (snippet->id));
    auto sanitized = defaultName.replaceCharacters ("<>:\"/\\|?*", "_");
    auto startingFile = startingFolder.getChildFile (sanitized + ".wav");

    activeFileChooser = std::make_unique<juce::FileChooser> (
        "Save snippet as WAV",
        startingFile,
        "*.wav",
        true);

    auto flags = juce::FileBrowserComponent::saveMode
               | juce::FileBrowserComponent::canSelectFiles
               | juce::FileBrowserComponent::warnAboutOverwriting;

    activeFileChooser->launchAsync (flags, [this, snippetId](const juce::FileChooser& chooser)
    {
        auto result = chooser.getResult();
        activeFileChooser.reset();

        if (result == juce::File())
        {
            sendNotification ("Save cancelled.", "info");
            return;
        }

        auto target = result;
        if (target.isDirectory())
            target = target.getChildFile ("snippet.wav");

        if (! target.hasFileExtension (".wav"))
            target = target.withFileExtension (".wav");

        auto folder = target.getParentDirectory();
        auto snippet = audioProcessor.getLibrary().findById (snippetId);
        if (snippet == nullptr)
        {
            sendNotification ("Snippet no longer exists.", "error");
            return;
        }

        juce::String outPath, outError;
        if (audioProcessor.getLibrary().saveSnippetToFolder (*snippet, folder, outPath, outError))
        {
            audioProcessor.getLibrary().markSaved (snippetId, outPath);
            audioProcessor.setLibraryFolder (folder);
            sendNotification ("Saved snippet to " + outPath, "ok");
        }
        else
        {
            sendNotification ("Save failed: " + outError, "error");
        }
    });
}

void BluePrinterWebViewEditor::pickLibraryFolder(const juce::File& startingFolder)
{
    activeFileChooser = std::make_unique<juce::FileChooser> (
        "Choose library folder",
        startingFolder,
        "",
        true);

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectDirectories;

    activeFileChooser->launchAsync (flags, [this](const juce::FileChooser& chooser)
    {
        auto result = chooser.getResult();
        activeFileChooser.reset();
        if (result == juce::File())
        {
            sendNotification ("Folder selection cancelled.", "info");
            return;
        }
        audioProcessor.setLibraryFolder (result);
        sendNotification ("Library folder set to " + result.getFullPathName(), "ok");
    });
}

void BluePrinterWebViewEditor::pickLibraryFolderThenSave(int pendingSnippetId)
{
    juce::File start (audioProcessor.getLibraryFolder());
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    activeFileChooser = std::make_unique<juce::FileChooser> (
        "Choose library folder, then save snippet",
        start,
        "",
        true);

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectDirectories;

    activeFileChooser->launchAsync (flags, [this, pendingSnippetId](const juce::FileChooser& chooser)
    {
        auto folder = chooser.getResult();
        activeFileChooser.reset();
        if (folder == juce::File())
        {
            sendNotification ("Folder selection cancelled.", "info");
            return;
        }
        audioProcessor.setLibraryFolder (folder);
        saveSnippetWithDialog (pendingSnippetId, folder);
    });
}

void BluePrinterWebViewEditor::sendNotification(const juce::String& message, const juce::String& level)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("message", message);
    obj->setProperty ("level", level);
    webView.emitEventIfBrowserIsVisible (juce::Identifier (backendNotifyEvent), juce::var (obj));
}
