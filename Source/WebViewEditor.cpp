#include "WebViewEditor.h"
#include "PluginChain.h"

namespace
{
// A DialogWindow that routes the native close (X) button to a
// std::function, so the editor can drop the matching entry from
// vst3EditorWindows. The default DialogWindow::closeButtonPressed()
// only hides the window, which leaves a dangling unique_ptr in the
// map and makes the next "Edit" click a no-op.
class Vst3EditorWindow : public juce::DialogWindow
{
public:
    Vst3EditorWindow (const juce::String& name,
                      juce::Colour bg,
                      bool escapeCloses,
                      bool hasTitleBar)
        : juce::DialogWindow (name, bg, escapeCloses, hasTitleBar)
    {
    }

    std::function<void()> onCloseRequested;

    void closeButtonPressed() override
    {
        if (onCloseRequested)
            onCloseRequested();
        else
            juce::DialogWindow::closeButtonPressed();
    }
};

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
    obj->setProperty ("key", s.key);
    obj->setProperty ("keyConfidence", s.keyConfidence);

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
        .withEventListener(BluePrinterWebViewEditor::frontendDetectSnippetKeyEvent, [&processor](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const int id = static_cast<int> (obj->getProperty ("id"));
                processor.detectSnippetKey (id);
            }
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
        .withEventListener(BluePrinterWebViewEditor::frontendRefreshLibraryEvent, [&processor](juce::var)
        {
            processor.refreshLibraryFromFolder();
        })
        .withEventListener(BluePrinterWebViewEditor::frontendSetMetronomeEvent, [&processor](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
                processor.setMetronomeEnabled (static_cast<bool> (obj->getProperty ("enabled")));
        })
        .withEventListener(BluePrinterWebViewEditor::frontendSetBpmEvent, [&processor](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
                processor.setBpm (static_cast<float> (obj->getProperty ("bpm")));
        })
        .withEventListener(BluePrinterWebViewEditor::frontendSetCountInBeatsEvent, [&processor](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
                processor.setCountInBeats (static_cast<int> (obj->getProperty ("beats")));
        })
        .withEventListener(BluePrinterWebViewEditor::frontendAddVst3Event, [owner](juce::var data)
        {
            if (owner == nullptr)
                return;

            // The "+ Add plugin…" button sends an empty payload and
            // wants the file picker. The available-plugins list sends
            // { path: "C:\\…\\Foo.vst3" } and wants a direct add.
            const auto path = data.getProperty ("path", {}).toString();
            if (path.isNotEmpty())
                owner->addVst3FromPath (juce::File (path));
            else
                owner->pickVst3FileAndAdd();
        })
        .withEventListener(BluePrinterWebViewEditor::frontendRemoveVst3Event, [&processor, owner](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const int index = static_cast<int> (obj->getProperty ("index"));
                if (owner != nullptr)
                    owner->closeVst3Editor (index, false);
                processor.getPluginChain().removePlugin (index);
                if (owner != nullptr)
                    owner->emitVst3ChainSnapshot();
            }
        })
        .withEventListener(BluePrinterWebViewEditor::frontendMoveVst3Event, [&processor, owner](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const int from = static_cast<int> (obj->getProperty ("from"));
                const int to   = static_cast<int> (obj->getProperty ("to"));
                if (processor.getPluginChain().movePlugin (from, to) && owner != nullptr)
                {
                    // The slots behind the moved one shifted index, so
                    // any open editor window keyed by the old index is
                    // now looking at the wrong plugin. Re-key the map
                    // by matching the window's plugin path against
                    // the new chain before the snapshot goes out.
                    owner->rekeyVst3EditorWindows();
                    owner->emitVst3ChainSnapshot();
                }
            }
        })
        .withEventListener(BluePrinterWebViewEditor::frontendSetVst3BypassEvent, [&processor, owner](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const int index = static_cast<int> (obj->getProperty ("index"));
                const bool bypass = static_cast<bool> (obj->getProperty ("bypassed"));
                processor.getPluginChain().setBypass (index, bypass);
                if (owner != nullptr)
                    owner->emitVst3ChainSnapshot();
            }
        })
        .withEventListener(BluePrinterWebViewEditor::frontendOpenVst3EditorEvent, [owner](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const int index = static_cast<int> (obj->getProperty ("index"));
                if (owner != nullptr)
                    owner->openVst3Editor (index);
            }
        })
        .withEventListener(BluePrinterWebViewEditor::frontendCloseVst3EditorEvent, [owner](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const int index = static_cast<int> (obj->getProperty ("index"));
                if (owner != nullptr)
                    owner->closeVst3Editor (index, true);
            }
        })
        .withEventListener(BluePrinterWebViewEditor::frontendScanVst3FolderEvent, [owner](juce::var)
        {
            if (owner != nullptr)
                owner->scanDefaultVst3Folder();
        })
        .withEventListener(BluePrinterWebViewEditor::frontendGetVst3ChainEvent, [owner](juce::var)
        {
            if (owner != nullptr)
                owner->emitVst3ChainSnapshot();
        })
        .withEventListener(BluePrinterWebViewEditor::frontendBlockVst3PluginEvent, [&processor, owner](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const auto path = obj->getProperty("path").toString();
                if (path.isNotEmpty())
                {
                    processor.getPluginChain().addToBlocklist (juce::File (path));
                    if (owner != nullptr)
                        owner->emitVst3ChainSnapshot();
                }
            }
        })
        .withEventListener(BluePrinterWebViewEditor::frontendUnblockVst3PluginEvent, [&processor, owner](juce::var data)
        {
            if (auto* obj = data.getDynamicObject())
            {
                const auto path = obj->getProperty("path").toString();
                if (path.isNotEmpty())
                {
                    processor.getPluginChain().removeFromBlocklist (juce::File (path));
                    if (owner != nullptr)
                        owner->emitVst3ChainSnapshot();
                }
            }
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

    // When a chain slot is removed (e.g. user removed the plugin), close
    // any open native editor for it so we don't leak a window with a
    // dangling plugin pointer.
    audioProcessor.getPluginChain().onSlotRemoved = [this] (int index)
    {
        const auto it = vst3EditorWindows.find (index);
        if (it != vst3EditorWindows.end())
            it->second.reset();
        vst3EditorWindows.erase (index);
    };

    // Push a fresh chain snapshot whenever the chain mutates so the UI
    // stays in sync (covers mutations from the message thread that
    // didn't go through the event listeners, like setChainState).
    audioProcessor.getPluginChain().onChanged = [this]
    {
        chainUpdatePending.store (true, std::memory_order_release);
        triggerAsyncUpdate();
    };

    // Push the initial chain snapshot so the UI doesn't sit empty until
    // the user makes a change. Also push the default VST3 folder so the
    // header can show it.
    chainUpdatePending.store (true, std::memory_order_release);
    triggerAsyncUpdate();

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
    // Expire any in-flight scan callbacks before tearing other state
    // down. describeVst3FileAsync captures a weak_ptr<int> to
    // aliveToken; resetting the token here invalidates that weak_ptr
    // and turns the callback into a no-op even if its timer fires
    // after this destructor returns. Set the disposed flag for the
    // same reason — the callback checks both before touching the
    // editor.
    aliveToken.reset();
    disposed.store (true, std::memory_order_release);

    cancelPendingUpdate();
    stopTimer();
    activeScan.reset();
    audioProcessor.apvts.removeParameterListener(paramGain, this);
    audioProcessor.removeListener (this);
    closeAllVst3Editors();
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
    if (chainUpdatePending.exchange(false, std::memory_order_acq_rel))
        emitVst3ChainSnapshot();
}

void BluePrinterWebViewEditor::timerCallback()
{
    // Throttled push: always push transport so the meter / timecode moves.
    emitTransportToFrontend();

    // Drive the VST3 scan one file at a time on the message thread so
    // the VST3 module is only touched from the right thread.
    if (activeScan != nullptr)
        runScanStep();
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

void BluePrinterWebViewEditor::pluginChainChanged()
{
    chainUpdatePending.store(true, std::memory_order_release);
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
    obj->setProperty ("metronomeEnabled", audioProcessor.getMetronomeEnabled());
    obj->setProperty ("bpm",              audioProcessor.getBpm());
    obj->setProperty ("countInBeats",     audioProcessor.getCountInBeats());
    obj->setProperty ("preRollActive",    audioProcessor.isPreRollActive());
    obj->setProperty ("transportPosition", static_cast<double> (audioProcessor.getTransportPosition()));
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

void BluePrinterWebViewEditor::openVst3Editor (int slotIndex)
{
    // If there's already an editor open for this slot, just bring it forward.
    if (vst3EditorWindows.count (slotIndex) > 0)
    {
        if (auto* existing = vst3EditorWindows[slotIndex].get())
            existing->toFront (true);
        return;
    }

    auto* plugin = audioProcessor.getPluginChain().getPlugin (slotIndex);
    if (plugin == nullptr)
        return;

    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
    if (editor == nullptr)
    {
        sendNotification ("This plugin has no editor UI.", "info");
        return;
    }

    auto title = plugin->getName() + " (FX slot " + juce::String (slotIndex + 1) + ")";
    auto window = std::make_unique<Vst3EditorWindow> (title,
                                                      juce::Colours::darkgrey,
                                                      true,  // has close button
                                                      true); // has title bar
    // The native X button must clean up the vst3EditorWindows entry.
    // Without this the window just hides, the map keeps a stale
    // unique_ptr, and the next "Edit" click is a no-op because
    // openVst3Editor thinks the editor is already open.
    window->onCloseRequested = [this, slotIndex]()
    {
        closeVst3Editor (slotIndex, true);
    };
    window->setContentOwned (editor.release(), true);
    window->centreWithSize (window->getContentComponent()->getWidth(),
                            window->getContentComponent()->getHeight());
    window->setVisible (true);

    vst3EditorWindows[slotIndex] = std::move (window);
    emitVst3ChainSnapshot();
}

void BluePrinterWebViewEditor::closeVst3Editor (int slotIndex, bool deleteAfterClose)
{
    juce::ignoreUnused (deleteAfterClose);
    const auto it = vst3EditorWindows.find (slotIndex);
    if (it == vst3EditorWindows.end())
        return;
    // Clear the close callback before destroying the window so a
    // re-entrant closeButtonPressed() (e.g. from the native X being
    // clicked as we're tearing down) doesn't try to call back into us
    // mid-destruction. The map's pointer type is juce::DialogWindow;
    // the actual object is a Vst3EditorWindow.
    if (auto* w = static_cast<Vst3EditorWindow*> (it->second.get()))
        w->onCloseRequested = nullptr;
    // Dropping the unique_ptr deletes the DialogWindow, which deletes the
    // AudioProcessorEditor (set via setContentOwned).
    vst3EditorWindows.erase (it);
    emitVst3ChainSnapshot();
}

void BluePrinterWebViewEditor::closeAllVst3Editors()
{
    vst3EditorWindows.clear();
}

void BluePrinterWebViewEditor::rekeyVst3EditorWindows()
{
    if (vst3EditorWindows.empty())
        return;

    // For each open window, find the slot index whose path matches
    // the slot the window was opened for. The plugin instance
    // survives the move (the move just shifts the slot index), so the
    // AudioProcessorEditor the window owns is still valid — we just
    // need to update the map key so emitVst3ChainSnapshot reports the
    // right openEditors list and the "Close" button stays on the same
    // row the user dragged.
    auto& chain = audioProcessor.getPluginChain();
    std::map<int, std::unique_ptr<juce::DialogWindow>> rebuilt;
    for (auto& entry : vst3EditorWindows)
    {
        const juce::String oldPath = chain.getSlotPath (entry.first);
        int newIndex = -1;
        if (oldPath.isNotEmpty())
        {
            for (int i = 0; i < chain.getNumPlugins(); ++i)
            {
                if (chain.getSlotPath (i) == oldPath)
                {
                    newIndex = i;
                    break;
                }
            }
        }
        if (newIndex >= 0)
            rebuilt[newIndex] = std::move (entry.second);
        // If the path is gone (e.g. the slot was removed) the unique_ptr
        // drops here, closing the window.
    }
    vst3EditorWindows = std::move (rebuilt);
}

void BluePrinterWebViewEditor::emitVst3ChainSnapshot()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("folder",     PluginChain::getDefaultVst3Folder().getFullPathName());
    obj->setProperty ("chain",      audioProcessor.getPluginChain().getChainState());

    // The cached scan result is owned by PluginChain so it survives
    // host save/load — see PluginChain::setAvailablePlugins /
    // getAvailablePlugins. Reading it here means the "Available
    // plugins" list is restored on launch without re-walking the
    // VST3 folder.
    {
        const auto available = audioProcessor.getPluginChain().getAvailablePlugins();
        if (auto* arr = available.getArray())
            obj->setProperty ("plugins", juce::var (*arr));
        else
            obj->setProperty ("plugins", juce::var (juce::Array<juce::var>()));
    }

    // Hand the blocklist to the UI so it can show the user which plugins
    // are being skipped and let them unblock entries.
    {
        juce::Array<juce::var> blocklist;
        for (const auto& path : audioProcessor.getPluginChain().getBlocklist())
            blocklist.add (path);
        obj->setProperty ("blocklist", blocklist);
    }

    // Tell the UI which FX slots currently have an open native editor
    // window, so it can show "Close" instead of "Edit" for those rows
    // and let the user close the window from the FX chain panel.
    {
        juce::Array<juce::var> openEditors;
        for (const auto& entry : vst3EditorWindows)
            openEditors.add (entry.first);
        obj->setProperty ("openEditors", openEditors);
    }

    obj->setProperty ("restoreError", audioProcessor.getLastChainRestoreError());

    webView.emitEventIfBrowserIsVisible (juce::Identifier (backendVst3ChainEvent), juce::var (obj));
}

void BluePrinterWebViewEditor::scanVst3Folder (const juce::File& folder)
{
    // Drop the request if a scan is already in flight so we don't race
    // on the active scan state.
    if (activeScan != nullptr)
        return;

    if (! folder.isDirectory())
    {
        sendNotification ("Folder not found: " + folder.getFullPathName(), "error");
        return;
    }

    auto state = std::make_unique<Vst3ScanState>();
    state->folder = folder;
    state->files  = folder.findChildFiles (juce::File::findFiles, false, "*.vst3");
    activeScan = std::move (state);

    // Push an empty chain snapshot so the UI can show the folder path
    // right away. The PluginChain's cached scan result is left alone
    // until the scan finishes; this just refreshes the frontend.
    {
        auto* empty = new juce::DynamicObject();
        empty->setProperty ("folder", folder.getFullPathName());
        empty->setProperty ("plugins", juce::var (juce::Array<juce::var>()));
        webView.emitEventIfBrowserIsVisible (juce::Identifier (backendVst3ChainEvent), juce::var (empty));
    }

    emitScanProgress (true, 0, activeScan->files.size(), juce::String());
}

void BluePrinterWebViewEditor::scanDefaultVst3Folder()
{
    scanVst3Folder (PluginChain::getDefaultVst3Folder());
}

void BluePrinterWebViewEditor::runScanStep()
{
    if (activeScan == nullptr)
        return;

    // The per-file describe runs on a worker thread (see
    // PluginChain::describeVst3FileAsync) so a misbehaving .vst3 —
    // modal license dialog, hung initialize(), throwing factory call —
    // can be timed out and skipped without ever blocking the message
    // thread. While a describe is in flight we just wait for its
    // callback to advance the index.
    if (activeScan->asyncScanInFlight)
        return;

    const int idx = activeScan->currentIndex;
    const int total = activeScan->files.size();

    if (idx >= total)
    {
        // Commit the accumulated plugin list to the PluginChain (which
        // persists it in the host state) and push the final snapshot.
        audioProcessor.getPluginChain().setAvailablePlugins (juce::var (activeScan->pluginArray));
        {
            auto* result = new juce::DynamicObject();
            result->setProperty ("folder", activeScan->folder.getFullPathName());
            result->setProperty ("plugins", juce::var (activeScan->pluginArray));
            webView.emitEventIfBrowserIsVisible (juce::Identifier (backendVst3ChainEvent), juce::var (result));
        }

        emitScanProgress (false, total, total, juce::String());
        activeScan.reset();
        return;
    }

    const auto& file = activeScan->files[idx];
    activeScan->asyncScanInFlight = true;

    // Same timeout as addPluginAsync: 10s is enough for any well-behaved
    // VST3 factory call, and short enough that one bad plugin doesn't
    // stall the whole folder scan.
    constexpr int kDescribeTimeoutMs = 10000;

    audioProcessor.getPluginChain().describeVst3FileAsync (
        file, kDescribeTimeoutMs,
        [this, file, weakToken = std::weak_ptr<int> (aliveToken)]
        (const PluginChain::DescribeResult& result)
        {
            // The editor may have been destroyed (or the scan reset)
            // while the worker was running. Bail out without touching
            // any editor state in that case — the DescribeWaiter is
            // self-deleting so we don't have to clean it up here.
            if (weakToken.expired() || disposed.load (std::memory_order_acquire))
                return;
            if (activeScan == nullptr)
                return;

            if (result.timedOut)
            {
                sendNotification ("Timed out scanning " + file.getFileName()
                                  + ". You can block this plugin so it isn't tried again.",
                                  "error");
            }
            else if (result.error.isNotEmpty())
            {
                sendNotification ("Failed to scan " + file.getFileName() + ": "
                                  + result.error, "error");
            }
            else if (auto* arr = result.descriptions.getArray())
            {
                for (const auto& v : *arr)
                    activeScan->pluginArray.add (v);
            }

            const int nextIndex = activeScan->currentIndex + 1;
            emitScanProgress (true, nextIndex, activeScan->files.size(), file.getFileName());
            activeScan->asyncScanInFlight = false;
            activeScan->currentIndex = nextIndex;
        });
}

void BluePrinterWebViewEditor::emitScanProgress (bool active, int current, int total, const juce::String& currentFile)
{
    auto* p = new juce::DynamicObject();
    p->setProperty ("active", active);
    p->setProperty ("current", current);
    p->setProperty ("total", total);
    p->setProperty ("folder", activeScan != nullptr ? activeScan->folder.getFullPathName() : juce::String());
    p->setProperty ("currentFile", currentFile);
    webView.emitEventIfBrowserIsVisible (juce::Identifier (backendVst3ScanProgressEvent), juce::var (p));
}

void BluePrinterWebViewEditor::addVst3FromPath (const juce::File& vst3File)
{
    if (! vst3File.existsAsFile())
    {
        sendNotification ("Plugin file not found: " + vst3File.getFullPathName(), "error");
        return;
    }

    // Async load with a 10s timeout. The worker thread runs the
    // VST3 factory call (which can pop up a modal license dialog
    // for expired-license plugins), so the message thread stays
    // responsive and we can bail out if the plugin hangs.
    constexpr int kLoadTimeoutMs = 10000;
    audioProcessor.getPluginChain().addPluginAsync (
        vst3File, kLoadTimeoutMs,
        [this, vst3File](int slotIndex,
                         const juce::String& name,
                         const juce::String& error,
                         bool timedOut)
        {
            if (timedOut)
            {
                auto* payload = new juce::DynamicObject();
                payload->setProperty ("path",   vst3File.getFullPathName());
                payload->setProperty ("name",   vst3File.getFileName());
                payload->setProperty ("error",  error);
                payload->setProperty ("timedOut", true);
                webView.emitEventIfBrowserIsVisible (
                    juce::Identifier (backendVst3LoadFailedEvent), juce::var (payload));
                sendNotification ("Timed out loading " + vst3File.getFileName()
                                  + ". You can block this plugin so it isn't tried again.", "error");
                return;
            }

            if (slotIndex < 0)
            {
                auto* payload = new juce::DynamicObject();
                payload->setProperty ("path",   vst3File.getFullPathName());
                payload->setProperty ("name",   vst3File.getFileName());
                payload->setProperty ("error",  error);
                payload->setProperty ("timedOut", false);
                webView.emitEventIfBrowserIsVisible (
                    juce::Identifier (backendVst3LoadFailedEvent), juce::var (payload));
                sendNotification ("Failed to add plugin: " + error, "error");
                return;
            }

            sendNotification ("Added " + vst3File.getFileName() + " (" + name + ")", "ok");
            emitVst3ChainSnapshot();
        });
}

void BluePrinterWebViewEditor::pickVst3FileAndAdd()
{
    auto start = PluginChain::getDefaultVst3Folder();
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    activeFileChooser = std::make_unique<juce::FileChooser> (
        "Add VST3 plugin",
        start,
        "*.vst3",
        true);

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectFiles;

    activeFileChooser->launchAsync (flags, [this](const juce::FileChooser& chooser)
    {
        auto result = chooser.getResult();
        activeFileChooser.reset();
        if (result == juce::File())
        {
            sendNotification ("Add plugin cancelled.", "info");
            return;
        }

        addVst3FromPath (result);
    });
}
