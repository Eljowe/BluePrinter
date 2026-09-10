/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebViewEditor.h"

#include <algorithm>
#include <set>
#include <thread>

//==============================================================================
// Crash diagnostics (Windows only). Records the chain-restore step that is
// currently running in a fixed buffer (safe to read from a crash handler —
// no heap, no locks). The unhandled-exception filter below writes it out
// together with a module-offset backtrace so a crash inside a hosted VST3
// DLL can be attributed to the exact step that triggered it.
namespace
{
    char crashOpBuffer[1024] = "no chain operation in progress";
}

void BluePrinterAudioProcessor::setCrashOp (const char* op, const char* detail)
{
    snprintf (crashOpBuffer, sizeof (crashOpBuffer), "%s%s%s",
              op != nullptr ? op : "?",
              detail != nullptr && detail[0] != 0 ? " " : "",
              detail != nullptr ? detail : "");
}

const char* BluePrinterAudioProcessor::getCrashOp()
{
    return crashOpBuffer;
}

std::atomic<bool> BluePrinterAudioProcessor::devicePrepared { false };

bool BluePrinterAudioProcessor::isDevicePrepared()
{
    return devicePrepared.load (std::memory_order_acquire);
}

//==============================================================================
// Plugin UI apartment (see the class comment in PluginProcessor.h).
// Windows-only: the pump is Win32-specific (the project is Windows-only
// per WebView2; the fallbacks below keep it compiling elsewhere).
#ifdef JUCE_WINDOWS
#include <windows.h>
#endif

PluginUiApartment& getPluginUiApartment()
{
    // Deliberately leaked: the apartment's thread lives for the whole
    // process. Destroying a running juce::Thread at process exit would
    // be UB; the OS reclaims everything when the process dies.
    static PluginUiApartment* const instance = new PluginUiApartment();
    return *instance;
}

PluginUiApartment::PluginUiApartment() : juce::Thread ("Plugin UI Apartment")
{
    startThread();
}

PluginUiApartment::~PluginUiApartment()
{
    // Never reached (process-lifetime singleton); kept for completeness.
}

void PluginUiApartment::post (std::function<void()> fn)
{
    {
        const juce::ScopedLock sl (tasksLock);
        tasks.push_back (std::move (fn));
    }

   #ifdef JUCE_WINDOWS
    // Wake the pump only once the thread's message queue exists (the
    // queue is created by the first user32 call inside run()); before
    // that the thread will simply notice the task when it starts
    // looping. PostThreadMessageW on a queue-less thread fails, so the
    // gate matters.
    if (queueReady.load (std::memory_order_acquire))
        PostThreadMessageW (static_cast<DWORD> (reinterpret_cast<intptr_t> (getThreadId())),
                            WM_APP + 1, 0, 0);
   #endif
}

void PluginUiApartment::run()
{
   #ifdef JUCE_WINDOWS
    // The first user32 call creates this thread's message queue; every
    // window a plugin creates from a task below is owned by this
    // thread and only ever pumped right here.
    MSG msg;
    PeekMessageW (&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    queueReady.store (true, std::memory_order_release);
   #else
    queueReady.store (true, std::memory_order_release);
   #endif

    for (;;)
    {
        std::function<void()> fn;
        {
            const juce::ScopedLock sl (tasksLock);
            if (! tasks.empty())
            {
                fn = std::move (tasks.front());
                tasks.pop_front();
            }
        }

        if (fn)
        {
            fn();
            continue;
        }

       #ifdef JUCE_WINDOWS
        // Park until a thread message arrives (task wake-ups come as
        // WM_APP+1). Window messages for plugin windows are dispatched
        // here — never by the app's main message loop.
        const DWORD waitResult = MsgWaitForMultipleObjectsEx (0, nullptr, INFINITE,
                                                              QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (waitResult == WAIT_FAILED)
            break;

        while (PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return;
            TranslateMessage (&msg);
            DispatchMessage (&msg);
        }
       #else
        juce::Thread::sleep (5);
       #endif
    }
}

#ifdef JUCE_WINDOWS
#include <windows.h>
static LONG WINAPI bluePrinterCrashHandler (PEXCEPTION_POINTERS info)
{
    // Only kernel32 calls (no CRT, no heap) so this is safe even on a
    // corrupted heap. Returns CONTINUE_SEARCH so WER still collects its
    // usual crash report on top of ours.
    char appData[MAX_PATH] = { 0 };
    GetEnvironmentVariableA ("APPDATA", appData, sizeof (appData));

    char path[MAX_PATH] = { 0 };
    wsprintfA (path, "%s\\Retrokielto\\crash-info.txt", appData);

    char text[4096] = { 0 };
    int len = wsprintfA (text,
                         "BluePrinter crash diagnostics\n"
                         "Operation: %s\n",
                         BluePrinterAudioProcessor::getCrashOp());

    const auto* addr = static_cast<const unsigned char*> (info->ExceptionRecord->ExceptionAddress);
    HMODULE mod = nullptr;
    char moduleName[MAX_PATH] = { 0 };
    if (GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCSTR> (addr), &mod) != 0 && mod != nullptr)
    {
        GetModuleFileNameA (mod, moduleName, sizeof (moduleName));
        len += wsprintfA (text + len,
                          "Faulting module: %s\nFault offset: 0x%I64x\n",
                          moduleName,
                          static_cast<unsigned long long> (addr - reinterpret_cast<const unsigned char*> (mod)));
    }
    else
    {
        len += wsprintfA (text + len,
                          "Faulting address: 0x%I64x\n",
                          reinterpret_cast<unsigned long long> (addr));
    }

    len += wsprintfA (text + len, "Backtrace (module, offset):\n");
    void* frames[16] = { nullptr };
    const int frameCount = static_cast<int> (CaptureStackBackTrace (0, 16, frames, nullptr));
    for (int i = 0; i < frameCount; ++i)
    {
        const auto* faddr = static_cast<const unsigned char*> (frames[i]);
        HMODULE fmod = nullptr;
        if (GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                reinterpret_cast<LPCSTR> (faddr), &fmod) != 0 && fmod != nullptr)
        {
            char fname[MAX_PATH] = { 0 };
            GetModuleFileNameA (fmod, fname, sizeof (fname));
            len += wsprintfA (text + len, "  [%02d] %s + 0x%I64x\n",
                              i, fname,
                              static_cast<unsigned long long> (faddr - reinterpret_cast<const unsigned char*> (fmod)));
        }
        else
        {
            len += wsprintfA (text + len, "  [%02d] 0x%I64x\n", i, reinterpret_cast<unsigned long long> (faddr));
        }
    }

    HANDLE f = CreateFileA (path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile (f, text, static_cast<DWORD> (len), &written, nullptr);
        CloseHandle (f);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

//==============================================================================
BluePrinterAudioProcessor::BluePrinterAudioProcessor()
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
#ifdef JUCE_WINDOWS
    SetUnhandledExceptionFilter (bluePrinterCrashHandler);
#endif

    // Restore the library folder at startup. VST3 chain restoration is
    // intentionally deferred until the editor requests it; constructing a
    // third-party plugin in the processor constructor can crash the
    // standalone before the UI is available to report the failing plugin.
    if (auto* props = getUserState())
    {
        const auto folderPath = props->getValue ("libraryFolder");
        if (folderPath.isNotEmpty())
        {
            const juce::File folder (folderPath);
            if (folder.isDirectory())
                setLibraryFolder (folder);
        }
    }

    // Seed the default chain layout (mirrors the pre-multi-chain
    // behaviour): a MIDI chain that sees the keyboard, and an audio FX
    // chain that doesn't. A saved state replaces these via
    // applyChainState. Restoring the just-loaded state back over the
    // file is prevented by the persistingPluginChain guard in
    // persistPluginChain. (createChain wires the persistence callback
    // on every chain, defaults included.)
    createChain ("MIDI Chain", ChainInputBoth, true, true);
    createChain ("Audio FX Chain", ChainInputBoth, false, true);
}

void BluePrinterAudioProcessor::setChainWantsMidi (const juce::String& chainId, bool enabled)
{
    if (auto* chain = getChainById (chainId))
    {
        chain->setWantsMidi (enabled);
        persistPluginChain();
        listeners.call ([](Listener& l) { l.pluginChainChanged(); });
    }
}

PluginChain* BluePrinterAudioProcessor::getChainById (const juce::String& chainId) const
{
    const juce::ScopedLock sl (chainLock);
    for (auto& chain : chains)
        if (chain->getChainId() == chainId)
            return chain.get();
    return nullptr;
}

juce::String BluePrinterAudioProcessor::addChain (const juce::String& name,
                                                 int inputMask,
                                                 bool wantsMidi,
                                                 bool recordOnCapture)
{
    auto* chain = createChain (name, inputMask, wantsMidi, recordOnCapture);
    if (chain == nullptr)
        return {};

    persistPluginChain();
    listeners.call ([](Listener& l) { l.pluginChainChanged(); });
    return chain->getChainId();
}

bool BluePrinterAudioProcessor::removeChain (const juce::String& chainId)
{
    std::unique_ptr<PluginChain> removed;
    {
        const juce::ScopedLock sl (chainLock);
        for (auto it = chains.begin(); it != chains.end(); ++it)
        {
            if ((*it)->getChainId() == chainId)
            {
                removed = std::move (*it);
                chains.erase (it);
                break;
            }
        }
    }

    if (removed == nullptr)
        return false;

    // Dropping every slot closes any open editor windows (via
    // onSlotRemoved) and releases the plugin instances before the
    // chain object itself is destroyed.
    removed->clear();
    persistPluginChain();
    listeners.call ([](Listener& l) { l.pluginChainChanged(); });
    return true;
}

bool BluePrinterAudioProcessor::renameChain (const juce::String& chainId, const juce::String& name)
{
    if (auto* chain = getChainById (chainId))
    {
        chain->setName (name);
        persistPluginChain();
        listeners.call ([](Listener& l) { l.pluginChainChanged(); });
        return true;
    }
    return false;
}

bool BluePrinterAudioProcessor::setChainInputs (const juce::String& chainId, int mask)
{
    if (auto* chain = getChainById (chainId))
    {
        chain->setInputMask (mask);
        persistPluginChain();
        listeners.call ([](Listener& l) { l.pluginChainChanged(); });
        return true;
    }
    return false;
}

bool BluePrinterAudioProcessor::setChainRecordOnCapture (const juce::String& chainId, bool enabled)
{
    if (auto* chain = getChainById (chainId))
    {
        chain->setRecordOnCapture (enabled);
        persistPluginChain();
        listeners.call ([](Listener& l) { l.pluginChainChanged(); });
        return true;
    }
    return false;
}

bool BluePrinterAudioProcessor::setChainVolume (const juce::String& chainId, float volumeDb)
{
    if (auto* chain = getChainById (chainId))
    {
        chain->setVolumeDb (juce::jlimit (-60.0f, 12.0f, volumeDb));
        // No pluginChainChanged notification: the panel tracks the knob
        // optimistically, and a full chain snapshot (which serializes
        // every plugin's state) per drag tick is what made the knob
        // laggy. Persistence is debounced too.
        persistPluginChain();
        return true;
    }
    return false;
}

bool BluePrinterAudioProcessor::setChainMute (const juce::String& chainId, bool muted)
{
    if (auto* chain = getChainById (chainId))
    {
        chain->setMuted (muted);
        // Same as setChainVolume: the toggle is optimistic in the UI.
        persistPluginChain();
        return true;
    }
    return false;
}

bool BluePrinterAudioProcessor::setChainMonitorSolo (const juce::String& chainId, bool solo)
{
    if (auto* chain = getChainById (chainId))
    {
        chain->setMonitorSolo (solo);
        persistPluginChain();
        listeners.call ([](Listener& l) { l.pluginChainChanged(); });
        return true;
    }
    return false;
}

bool BluePrinterAudioProcessor::setChainMonitorMute (const juce::String& chainId, bool muted)
{
    if (auto* chain = getChainById (chainId))
    {
        chain->setMonitorMuted (muted);
        persistPluginChain();
        listeners.call ([](Listener& l) { l.pluginChainChanged(); });
        return true;
    }
    return false;
}

bool BluePrinterAudioProcessor::setChainMidiChannels (const juce::String& chainId, uint16_t mask)
{
    if (auto* chain = getChainById (chainId))
    {
        chain->setMidiChannelsMask (mask);
        persistPluginChain();
        listeners.call ([](Listener& l) { l.pluginChainChanged(); });
        return true;
    }
    return false;
}

PluginChain* BluePrinterAudioProcessor::createChain (const juce::String& name,
                                                     int inputMask,
                                                     bool wantsMidi,
                                                     bool recordOnCapture)
{
    auto chain = std::make_unique<PluginChain> (vst3Library);
    chain->setChainId (juce::String ("chain") + juce::String (nextChainId++));
    chain->setName (name.isNotEmpty() ? name : juce::String ("Chain ") + juce::String (nextChainId));
    chain->setInputMask (inputMask);
    chain->setWantsMidi (wantsMidi);
    chain->setRecordOnCapture (recordOnCapture);

    PluginChain* raw = chain.get();
    {
        const juce::ScopedLock sl (chainLock);
        chains.push_back (std::move (chain));
    }
    // Every chain (default, UI-created, or restore-created) must run
    // the persistence callback on change. Wiring it here — rather than
    // only on the constructor's default chains — is what makes slot
    // mutations (add/remove/bypass) on restored chains persist.
    raw->onChanged = [this] { persistPluginChain(); };
    return raw;
}

void BluePrinterAudioProcessor::clearChains()
{
    std::vector<std::unique_ptr<PluginChain>> removed;
    {
        const juce::ScopedLock sl (chainLock);
        removed = std::move (chains);
    }
    for (auto& chain : removed)
        chain->clear();
}

void BluePrinterAudioProcessor::ensureUniqueChainIds()
{
    std::set<juce::String> seen;
    int maxId = -1;
    {
        const juce::ScopedLock sl (chainLock);

        // First pass: find the highest numeric id in use ("chainN"),
        // so regenerated ids never collide with existing ones.
        for (auto& chain : chains)
        {
            const juce::String id = chain->getChainId();
            if (id.startsWith ("chain") && id.length() > 5)
            {
                const juce::String suffix = id.substring (5);
                const int numeric = suffix.getIntValue();
                if (suffix == juce::String (numeric))
                    maxId = juce::jmax (maxId, numeric);
            }
        }

        // Second pass: assign fresh ids to missing/duplicate ids,
        // starting above the highest id in use.
        int next = juce::jmax (maxId + 1, nextChainId);
        for (auto& chain : chains)
        {
            const juce::String id = chain->getChainId();
            if (id.isEmpty() || seen.count (id) > 0)
                chain->setChainId (juce::String ("chain") + juce::String (next++));
            else
                seen.insert (id);
        }
        nextChainId = next;
    }
}

BluePrinterAudioProcessor::~BluePrinterAudioProcessor()
{
    stopTimer();
    // Persist any debounced chain change so the very last mutation of a
    // session (e.g. a volume knob drag finished moments before closing)
    // is not lost.
    flushPendingChainPersist();
}

//==============================================================================
const juce::String BluePrinterAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool BluePrinterAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool BluePrinterAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool BluePrinterAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double BluePrinterAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int BluePrinterAudioProcessor::getNumPrograms()
{
    return 1;
}

int BluePrinterAudioProcessor::getCurrentProgram()
{
    return 0;
}

void BluePrinterAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String BluePrinterAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void BluePrinterAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void BluePrinterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // The real device configuration is now known; plugin loads from
    // here on eagerly prepare with the actual values (see
    // isDevicePrepared in the header).
    devicePrepared.store (true, std::memory_order_release);

    const int channels = juce::jmax (1, getTotalNumInputChannels());
    const auto maxSamples = static_cast<int> (sampleRate * maxRecordingSeconds);

    recordBuffer = std::make_unique<juce::AudioBuffer<float>> (channels, maxSamples);
    recordBuffer->clear();
    maxRecordSamples = maxSamples;
    recordWritePos.store (0, std::memory_order_release);

    // Per-block scratch buffers for the chain routing — see the
    // member comments. Sized to the input channel count like the old
    // midiChainBuffer scratch.
    chainInputBuffer.setSize (channels, samplesPerBlock, false, false, true);
    chainScratchBuffer.setSize (channels, samplesPerBlock, false, false, true);
    recordingMixBuffer.setSize (channels, samplesPerBlock, false, false, true);
    blockChains.clear();

    currentSampleRate = sampleRate;
    loopCrossfadeSamples = juce::jlimit (1, 256, static_cast<int> (sampleRate * 0.003));

    // Hand the new rate/block size to every chain so all loaded
    // plugins are prepared with the right values.
    {
        const juce::ScopedLock sl (chainLock);
        for (auto& chain : chains)
            chain->prepareToPlay (sampleRate, samplesPerBlock);
    }

    // Synthesize the metronome clicks from the current click
    // parameters. Two distinct sounds, rendered on the message thread
    // and read-only on the audio thread:
    //   - clickBuffer:      the normal beat tick (clickPitch, snappy)
    //   - accentClickBuffer: the first beat of each bar
    //     (clickAccentPitch, louder, slightly longer)
    // Both are short percussive bursts. A 2 ms linear attack ramp
    // starts at zero so the mix-in doesn't pop at the beat boundary; a
    // fast exponential decay plus a 2 ms tail fade end the sound
    // smoothly; a low-level deterministic noise transient during the
    // first few ms gives it the woodblock "tick" attack.
    resynthesizeClicks();

    startTimerHz (transportTimerHz);
}

void BluePrinterAudioProcessor::releaseResources()
{
    stopTimer();
    // Flush the debounced chain save; the 30 Hz timer that would do it
    // is being stopped and the host may tear the plugin down.
    flushPendingChainPersist();
    flushTagNamePersist();
    preRollActive.store (false, std::memory_order_release);
    transportPosition.store (0, std::memory_order_release);
    if (recordingRequested.load())
        stopRecording();
    if (playbackActive.load())
        stopPlayback();

    // Finalise any in-flight recording so the take isn't lost when the host
    // tears the plugin down. The audio thread is no longer running at this
    // point, so it's safe to copy the buffer here.
    if (recordWritePos.load() > 0)
    {
        recordingFinalizePending.store (true, std::memory_order_release);
        finalizeRecordingOnMessageThread();
    }

    recordBuffer.reset();
    maxRecordSamples = 0;
    recordWritePos.store (0, std::memory_order_release);
    playbackSnippet.reset();

    {
        const juce::ScopedLock sl (chainLock);
        for (auto& chain : chains)
            chain->releaseResources();
    }
    clickBuffer.reset();
    accentClickBuffer.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool BluePrinterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // Accept any layout with 1..8 input channels and the same output
    // channel count, so a multi-input interface can route separate
    // inputs to separate chains. Stereo remains the preferred default.
    const auto inSet  = layouts.getMainInputChannelSet();
    const auto outSet = layouts.getMainOutputChannelSet();

    if (inSet == juce::AudioChannelSet::disabled() || outSet == juce::AudioChannelSet::disabled())
        return false;

    const int numIn  = inSet.size();
    const int numOut = outSet.size();
    if (numIn < 1 || numIn > 8 || numOut != numIn)
        return false;

    return true;
  #endif
}
#endif

void BluePrinterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Where the metronome beat clock starts this block. The transport
    // steps (count-in / recording / looper) advance it below; if none
    // of them run but the MIDI clock is enabled, we advance it
    // ourselves so the clock free-runs without recording.
    const int64_t blockStartMetronomePos = metronomePosition.load (std::memory_order_acquire);
    // Set when any step below advances the beat clock this block, so the
    // count-in-into-capture transition (two drivers fire back-to-back in
    // the same block) advances exactly once per block.
    bool clockAdvancedThisBlock = false;

    // Apply the input trim (the record level). This is the post-DSP
    // signal we want to record and the pass-through signal when nothing
    // else is happening. The parameter is stored in dB.
    const auto inputGain = juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue ("Gain")->load());
    for (int channel = 0; channel < numChannels; ++channel)
        buffer.applyGain (channel, 0, numSamples, inputGain);

    // 1. Snapshot the chain list so a message-thread add/remove
    //    mid-block can't invalidate our iteration. Same raw-pointer
    //    snapshot model PluginChain uses for its slots.
    {
        const juce::ScopedLock sl (chainLock);
        blockChains.clear();
        for (auto& chain : chains)
            blockChains.push_back (chain.get());
    }

    // 1b. Pristine dry-input snapshot. Every chain copies its selected
    //    channels from this buffer, so chains are fully parallel —
    //    chain N can never hear chain M's output.
    {
        const int dryCh = juce::jmin (numChannels, chainInputBuffer.getNumChannels());
        for (int ch = 0; ch < dryCh; ++ch)
            chainInputBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    // 1c. Scale the direct dry pass-through. The chains already have the
    //    full input in chainInputBuffer, so this only controls how much
    //    raw dry is heard and printed — at the minimum the dry is gone
    //    and only the chains are audible (and captured).
    const float dryGain = juce::Decibels::decibelsToGain (
        dryLevel.load (std::memory_order_acquire));
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.applyGain (ch, 0, numSamples, dryGain);

    // 2. Run the chains in parallel. Every chain gets its own scratch
    //    copy of the input channels it selected, processes it in place,
    //    and its output is summed into the main buffer alongside the
    //    dry signal (scaled by the chain's volume, unless muted).
    //    Chains read from chainInputBuffer — a pristine snapshot of the
    //    post-gain input taken above — never from the accumulating mix,
    //    so one chain can never process another chain's output.
    //    Each chain also gets its own copy of the MIDI buffer (filtered
    //    to the chain's MIDI channel selection), so notes can't leak
    //    between chains, and the host's output MIDI stays as the raw
    //    input. recordingMixBuffer starts as the dry post-gain input
    //    (scaled by the Dry level) and accumulates only the chains whose
    //    record toggle is on — that is what captures (take recorder +
    //    looper) record.
    recordingMixBuffer.clear();
    {
        const int mixCh = juce::jmin (numChannels, recordingMixBuffer.getNumChannels());
        for (int ch = 0; ch < mixCh; ++ch)
            recordingMixBuffer.copyFrom (ch, 0, chainInputBuffer, ch, 0, numSamples);
    }
    recordingMixBuffer.applyGain (dryGain);

    // Monitor solo: if any chain is soloed, the monitor mix becomes only
    // the soloed chains, and the direct dry pass-through (already in
    // buffer) is muted for true isolation. The capture was built from
    // chainInputBuffer above, so it is untouched — solo/monitor-mute are
    // monitoring-only.
    bool soloActive = false;
    for (auto* chain : blockChains)
        if (chain->isMonitorSolo())
            soloActive = true;
    if (soloActive)
        buffer.clear();

    for (auto* chain : blockChains)
    {
        // A chain with no active plugins is transparent: its scratch
        // would merely hold a copy of the dry input, so summing it
        // back into the mix would double (or triple) the dry signal.
        // Skip it entirely.
        if (! chain->hasActivePlugins())
        {
            chain->outputLevel.store (0.0f, std::memory_order_release);
            chain->outputPeak.store  (0.0f, std::memory_order_release);
            continue;
        }

        chainScratchBuffer.clear();
        const int mask = chain->getInputMask();
        const int scratchCh = chainScratchBuffer.getNumChannels();
        for (int ch = 0; ch < scratchCh; ++ch)
        {
            // Only copy channels the chain selected AND that actually
            // exist in this block (mono layout: other channels stay
            // silent).
            if ((mask & (1 << ch)) != 0 && ch < numChannels)
                chainScratchBuffer.copyFrom (ch, 0, chainInputBuffer, ch, 0, numSamples);
        }

        // Per-chain MIDI copy so one chain's generated notes can't leak
        // into another. Copies into the member's existing storage (no
        // allocation unless the input MIDI grows beyond its capacity).
        // The chain's MIDI channel filter is applied here too.
        chainMidiScratch = midiMessages;
        juce::MidiBuffer* midiForChain = &chainMidiScratch;
        const uint16_t midiMask = chain->getMidiChannelsMask();
        if (midiMask != 0xFFFF)
        {
            chainMidiFiltered.clear();
            juce::MidiBuffer::Iterator it (chainMidiScratch);
            juce::MidiMessage msg;
            int samplePos = 0;
            while (it.getNextEvent (msg, samplePos))
            {
                if (chain->acceptsMidiChannel (msg.getChannel()))
                    chainMidiFiltered.addEvent (msg, samplePos);
            }
            midiForChain = &chainMidiFiltered;
        }
        chain->processBlock (chainScratchBuffer, *midiForChain);

        const int sumCh = juce::jmin (numChannels, chainScratchBuffer.getNumChannels());

        // Output volume + hard mute. Muted chains still run (their
        // plugins keep internal state consistent) but contribute nothing
        // to the mix or the capture.
        const float hardGain = chain->isMuted()
            ? 0.0f
            : juce::Decibels::decibelsToGain (chain->getVolumeDb());

        // Monitor contribution: with any chain soloed only the soloed
        // chains are heard (the dry was zeroed above); monitor-mute
        // removes just this chain. Neither touches the capture below,
        // which follows the hard mute + record flags.
        const bool monitorOn = (soloActive ? chain->isMonitorSolo() : true)
                               && ! chain->isMonitorMuted();
        const float monitorGain = monitorOn ? hardGain : 0.0f;

        for (int ch = 0; ch < sumCh; ++ch)
            buffer.addFrom (ch, 0, chainScratchBuffer, ch, 0, numSamples, monitorGain);

        if (chain->isRecordOnCapture() && hardGain > 0.0f)
        {
            const int mixCh = juce::jmin (recordingMixBuffer.getNumChannels(), sumCh);
            for (int ch = 0; ch < mixCh; ++ch)
                recordingMixBuffer.addFrom (ch, 0, chainScratchBuffer, ch, 0, numSamples, hardGain);
        }

        // Per-chain output meter (post-volume; muted chains read 0).
        // Independent of solo/monitor-mute.
        computeLevelsInto (chainScratchBuffer, numSamples,
                           chain->outputLevel, chain->outputPeak, hardGain);
    }

    // 2b. Record meter. recordingMixBuffer is the actual print (dry +
    //     recordOnCapture chains), so this reflects what a take or loop
    //     capture would store — independently of the master Output.
    computeLevelsInto (recordingMixBuffer, numSamples,
                       recordLevel, recordPeak, 1.0f, &recordClipped);

    // 3. Looper capture: tap the record mix so the loop bakes in
    //    whatever the selected chains produce (synth sounds, FX).
    //    Deliberately before the click is mixed in so the click never
    //    ends up in the loop. Uses the same pre-allocated recordBuffer
    //    as the take recorder. In overdub mode the new layer is written
    //    into the region after the existing loop (overdubWritePos) while
    //    audioLoopLength stays fixed, so the loop's wrap boundary never
    //    moves mid-capture; the layer is mixed into the loop on stop.
    if (looperCaptureArmed.load (std::memory_order_acquire))
    {
        const auto writePos = looperOverdubCapture.load (std::memory_order_acquire)
            ? overdubWritePos.load (std::memory_order_relaxed)
            : audioLoopLength.load (std::memory_order_relaxed);
        const auto toCopy = juce::jmin (numSamples, maxRecordSamples - static_cast<int> (writePos));
        if (toCopy > 0)
        {
            for (int ch = 0; ch < juce::jmin (numChannels, recordBuffer->getNumChannels()); ++ch)
                recordBuffer->copyFrom (ch, static_cast<int> (writePos), recordingMixBuffer, ch, 0, toCopy);
            if (looperOverdubCapture.load (std::memory_order_acquire))
                overdubWritePos.store (writePos + toCopy, std::memory_order_release);
            else
                audioLoopLength.store (writePos + toCopy, std::memory_order_release);
        }
    }

    // 4. Record the clean (post-gain, pre-click) record mix. Access to
    //    the record buffer is serialised with the message thread via
    //    recordLock.
    {
        const juce::ScopedLock sl (recordLock);
        if (recordingRequested.load (std::memory_order_acquire))
            writeRecording (recordingMixBuffer, numSamples);
    }

    // 5. Compute input levels from the clean post-gain dry snapshot so
    //    neither the click nor chain processing (nor monitor solo, which
    //    clears the dry from the monitor buffer) pumps the meter.
    computeLevels (chainInputBuffer, numSamples);

    // 6. Audio loop playback. Runs after the chains so the already-
    //    processed loop audio isn't re-processed (it was captured
    //    post-chain). Mixed over the live input rather than replacing
    //    it, so you can play over the loop. Reads the cropped window
    //    [audioLoopStart, audioLoopStart + audioLoopLength). The whole
    //    block is filled from the loop — it keeps playing across its
    //    wrap instead of leaving the block's remainder as silent
    //    live-through (a "goes silent" hole at every loop cycle).
    if (audioLoopPlaying.load (std::memory_order_acquire))
    {
        const auto start = audioLoopStart.load (std::memory_order_acquire);
        const auto length = audioLoopLength.load (std::memory_order_acquire);
        if (length > 0 && recordBuffer != nullptr)
        {
            const int channels = juce::jmin (numChannels, recordBuffer->getNumChannels());
            const int declick = juce::jmin (loopCrossfadeSamples, static_cast<int> (length / 2));
            const bool looping = looperLooping.load (std::memory_order_acquire);
            const float loopGain = juce::Decibels::decibelsToGain (
                loopLevel.load (std::memory_order_acquire));
            auto position = audioLoopPosition.load (std::memory_order_acquire);

            // Per-sample playback so each cycle can be de-clicked with a
            // short fade in/out at the seam. The old tail-into-head
            // crossfade injected the loop head (the crop start) at the END
            // of the preceding cycle, so on the second playthrough the loop
            // sounded like it restarted late (its head had already played).
            // This adds only the loop's own contribution on top of whatever
            // the live mix already holds, so direct monitoring is never
            // ducked at the seam either. The phase wraps to 0, so every
            // cycle starts exactly at audioLoopStart.
            float loopPeakThisBlock = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                if (position >= length)
                {
                    if (! looping)
                        break;
                    position = 0;
                }

                float env = 1.0f;
                if (declick > 0)
                {
                    const float fadeIn = static_cast<float> (position + 1)
                                       / static_cast<float> (declick + 1);
                    const float fadeOut = static_cast<float> (length - position)
                                        / static_cast<float> (declick + 1);
                    env = juce::jmin (1.0f, juce::jmin (fadeIn, fadeOut));
                }

                const int src = static_cast<int> (start + position);
                for (int ch = 0; ch < channels; ++ch)
                {
                    const float v = recordBuffer->getSample (ch, src) * loopGain * env;
                    loopPeakThisBlock = juce::jmax (loopPeakThisBlock, std::abs (v));
                    buffer.addSample (ch, i, v);
                }

                ++position;
            }

            // Loop playback meter (post loop-level gain, monitor only).
            // The peak decays between blocks like the other meters; the
            // level decays in timerCallback when playback stops.
            {
                const float prevLoopPeak = loopPlayPeak.load (std::memory_order_acquire);
                loopPlayPeak.store (juce::jmax (loopPeakThisBlock, prevLoopPeak * 0.95f),
                                    std::memory_order_release);
                loopPlayLevel.store (loopPeakThisBlock, std::memory_order_release);
                if (loopPeakThisBlock >= 1.0f)
                    loopPlayClipped.store (true, std::memory_order_release);
            }

            audioLoopPosition.store (position, std::memory_order_release);
            if (! looping && position >= length)
                audioLoopPlaying.store (false, std::memory_order_release);
        }
    }

    // 7. Playback overwrites the output buffer. Done after recording so
    //    monitoring of the input stops while a snippet is playing.
    if (playbackActive.load (std::memory_order_acquire))
        renderPlayback (buffer, numSamples);

    // 7b. Take-review playback. Rendered the same way (overwrites the
    //    output) so the user hears exactly the take that was captured.
    //    Mutually exclusive with snippet/looper playback — the entry
    //    points stop each other.
    if (takePlaybackActive.load (std::memory_order_acquire))
        renderTakePlayback (buffer, numSamples);

    // 8. Looper count-in: play the click, advance the beat clock, and flip
    //    into capture once the configured beats have elapsed. Mirrors the
    //    take-recorder pre-roll below but drives the looper's own capture
    //    state. Rendered post-chain so the click is at the same level and
    //    colour as the take recorder's.
    if (looperPreRollActive.load (std::memory_order_acquire))
    {
        const int64_t startPos = metronomePosition.load (std::memory_order_acquire);
        if (metronomeEnabled.load (std::memory_order_acquire))
            renderMetronomeInBlock (buffer, startPos, numSamples);

        const int64_t newPos = startPos + numSamples;
        const double bpmValue = bpm.load (std::memory_order_acquire);
        const int beatsTarget = looperCountInBeats.load (std::memory_order_acquire);

        bool done = true;
        if (bpmValue > 0.0 && currentSampleRate > 0.0)
        {
            const double samplesPerBeat = 60.0 / bpmValue * currentSampleRate;
            done = static_cast<int> (newPos / samplesPerBeat) >= beatsTarget;
        }

        if (done)
        {
            looperPreRollActive.store (false, std::memory_order_release);
            looperCaptureArmed.store (true, std::memory_order_release);
            audioLoopRecording.store (true, std::memory_order_release);
            // Overdub captures keep the existing loop length — only a
            // fresh capture resets it. Start the loop from the top exactly
            // when the layer capture begins so the layer aligns with the
            // loop downbeat (the count-in played over silence).
            if (! looperOverdubCapture.load (std::memory_order_acquire))
            {
                audioLoopLength.store (0, std::memory_order_release);
            }
            else
            {
                audioLoopPosition.store (0, std::memory_order_release);
                audioLoopPlaying.store (true, std::memory_order_release);
            }
            // Capture is starting: re-sync external gear when the clock
            // is already running (the header-level clock toggle).
            if (clockRunning.load (std::memory_order_acquire))
                midiStartPending.store (true, std::memory_order_release);
        }
        metronomePosition.store (newPos, std::memory_order_release);
        transportPosition.store (newPos, std::memory_order_release);
        clockAdvancedThisBlock = true;
    }

    // 9. Pre-roll (count-in) for the take recorder: add the click to the
    //    output, advance the position, and flip into recording once the
    //    configured number of beats has elapsed. Uses metronomePosition as
    //    the continuous beat clock so counts stay evenly spaced across the
    //    transition into recording — no double-click mid-block.
    if (preRollActive.load (std::memory_order_acquire))
    {
        const int64_t startPos = metronomePosition.load (std::memory_order_acquire);
        renderMetronomeInBlock (buffer, startPos, numSamples);

        const int64_t newPos = startPos + numSamples;
        const double bpmValue = bpm.load (std::memory_order_acquire);
        const int beatsTarget   = countInBeats.load (std::memory_order_acquire);

        bool done = true;
        if (bpmValue > 0.0 && currentSampleRate > 0.0)
        {
            const double samplesPerBeat = 60.0 / bpmValue * currentSampleRate;
            const int beatsElapsed = static_cast<int> (newPos / samplesPerBeat);
            done = beatsElapsed >= beatsTarget;
        }

        if (done)
        {
            preRollActive.store (false, std::memory_order_release);
            transportPosition.store (0, std::memory_order_release);
            metronomePosition.store (newPos, std::memory_order_release);
            clockAdvancedThisBlock = true;
            beginActualRecording();
        }
        else
        {
            transportPosition.store (newPos, std::memory_order_release);
            metronomePosition.store (newPos, std::memory_order_release);
            clockAdvancedThisBlock = true;
        }
    }

    // 10. Click during recording. The metronome beat clock runs
    //    continuously from the recording start (or count-in end) so
    //    beats land at evenly-spaced positions regardless of when the
    //    recording was started. The clock keeps advancing even when
    //    the metronome is muted, so toggling the metronome back on
    //    doesn't shift the beat grid. clickDuringCapture off = the
    //    click only plays during the count-in, never through the take.
    if (recordingRequested.load (std::memory_order_acquire)
        && ! clockAdvancedThisBlock)
    {
        const int64_t startPos = metronomePosition.load (std::memory_order_acquire);
        if (metronomeEnabled.load (std::memory_order_acquire)
            && clickDuringCapture.load (std::memory_order_acquire))
            renderMetronomeInBlock (buffer, startPos, numSamples);

        const int64_t newPos = startPos + numSamples;
        metronomePosition.store (newPos, std::memory_order_release);
        transportPosition.store (newPos, std::memory_order_release);
        clockAdvancedThisBlock = true;
    }

    // 11. Click during looper capture. Same beat clock, so the looper's
    //    count-in flows straight into capture with evenly spaced beats.
    //    Mixed after the capture tap so the click never lands in the loop.
    //    The same header-level clickDuringCapture gates the click — off
    //    means count-in only, never through the capture itself.
    if (looperCaptureArmed.load (std::memory_order_acquire)
        && ! clockAdvancedThisBlock)
    {
        const int64_t startPos = metronomePosition.load (std::memory_order_acquire);
        if (metronomeEnabled.load (std::memory_order_acquire)
            && clickDuringCapture.load (std::memory_order_acquire))
            renderMetronomeInBlock (buffer, startPos, numSamples);

        const int64_t newPos = startPos + numSamples;
        metronomePosition.store (newPos, std::memory_order_release);
        transportPosition.store (newPos, std::memory_order_release);
        clockAdvancedThisBlock = true;
    }

    // 12. MIDI clock output. Clock pulses (0xF8) are generated at
    //    24 ppqn from the continuous metronomePosition so they align
    //    with the audible metronome and run through count-in into the
    //    recording. The header-level clock toggle keeps it alive: with
    //    midiClockOnRecord off the clock advances the position itself
    //    so it runs free — the user can drive a drum machine's presets
    //    without recording, and the audible click plays along so the
    //    beats can be heard (subject to the metronome toggle). With
    //    midiClockOnRecord on, the clock only runs while a take or
    //    loop capture is active (count-in included) and stops when the
    //    capture ends. Takes and loop captures ride the clock either
    //    way, re-syncing with a Start at actual-recording / capture
    //    time. Queued MIDI Start / Stop are flushed here so the
    //    receiver gets them at a block boundary.
    {
        refreshClockRunning();

        const bool clockEnabled = clockRunning.load (std::memory_order_acquire);
        const bool clockFreeRan = clockEnabled
            && metronomePosition.load (std::memory_order_acquire) == blockStartMetronomePos;

        if (clockFreeRan)
        {
            metronomePosition.store (blockStartMetronomePos + numSamples,
                                     std::memory_order_release);
            clockAdvancedThisBlock = true;

            // Sound the click for the free-running clock so the beats
            // are audible without recording or looping. The header-level
            // clock toggle free-runs; takes and loop captures ride the
            // same clock and gate their own click with clickDuringCapture.
            if (midiClockEnabled.load (std::memory_order_acquire)
                && metronomeEnabled.load (std::memory_order_acquire))
                renderMetronomeInBlock (buffer, blockStartMetronomePos, numSamples);
        }

        const int64_t clockPos = metronomePosition.load (std::memory_order_acquire);
        renderMidiClockInBlock (midiMessages, clockPos, numSamples);

        // Flush pending start / stop at sample 0 so downstream
        // hardware sees the command before the next clock pulse.
        if (midiStartPending.exchange (false, std::memory_order_acquire))
            midiMessages.addEvent (juce::MidiMessage::midiStart(), 0);
        if (midiStopPending.exchange (false, std::memory_order_acquire))
            midiMessages.addEvent (juce::MidiMessage::midiStop(), 0);
    }

    // 13. Master monitor output. Applied last so it scales everything the
    //     user hears (live mix, loop, take/snippet playback, click)
    //     without touching the recording capture — recordingMixBuffer
    //     was written above — or the loop capture tap. The parameter is
    //     stored in dB.
    {
        const auto outputGain = juce::Decibels::decibelsToGain (
            apvts.getRawParameterValue ("PlaybackVolume")->load());
        for (int channel = 0; channel < numChannels; ++channel)
            buffer.applyGain (channel, 0, numSamples, outputGain);
    }

    // 14. Output meter: the post-master-Output monitor signal — what the
    //     user actually hears. Computed last so it includes the click and
    //     any snippet/loop playback.
    computeLevelsInto (buffer, numSamples, outputLevel, outputPeak, 1.0f, &outputClipped);
}

void BluePrinterAudioProcessor::writeRecording (const juce::AudioBuffer<float>& source, int numSamples)
{
    if (recordBuffer == nullptr || maxRecordSamples <= 0)
        return;

    auto writePos = static_cast<int> (recordWritePos.load (std::memory_order_acquire));
    if (writePos >= maxRecordSamples)
        return;

    const int channels = juce::jmin (source.getNumChannels(), recordBuffer->getNumChannels());
    const int toCopy   = juce::jmin (numSamples, maxRecordSamples - writePos);

    for (int ch = 0; ch < channels; ++ch)
        recordBuffer->copyFrom (ch, writePos, source, ch, 0, toCopy);

    writePos += toCopy;
    recordWritePos.store (writePos, std::memory_order_release);

    if (writePos >= maxRecordSamples)
    {
        recordingRequested.store (false, std::memory_order_release);
        recordingState.store (RecordingState::Idle, std::memory_order_release);
        recordingFinalizePending.store (true, std::memory_order_release);
    }
}

void BluePrinterAudioProcessor::renderClickTail (juce::AudioBuffer<float>& buffer,
                                                 ActiveClick& ac,
                                                 int64_t startPos,
                                                 int64_t endPos,
                                                 int numChannels)
{
    if (ac.buffer == nullptr || ac.readPos >= static_cast<int> (ac.buffer->size()))
        return;

    // Where this click's unplayed samples would land.
    const int64_t head = juce::jmax (startPos, ac.nextSample);
    const int64_t tailEnd = ac.nextSample
                        + (static_cast<int> (ac.buffer->size()) - ac.readPos);
    if (tailEnd <= startPos)
        return;

    const int64_t inBlockEnd = juce::jmin (endPos, tailEnd);
    if (inBlockEnd <= head)
        return;

    const int blockOff = static_cast<int> (head - startPos);
    const int count    = static_cast<int> (inBlockEnd - head);
    for (int j = 0; j < count; ++j)
    {
        const float sample = (*ac.buffer)[static_cast<size_t> (ac.readPos + j)];
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addSample (ch, blockOff + j, sample);
    }

    ac.readPos    += count;
    ac.nextSample += count;
}

void BluePrinterAudioProcessor::renderMetronomeInBlock (juce::AudioBuffer<float>& buffer,
                                                        int64_t startPos,
                                                        int numSamples)
{
    // Copy the shared_ptrs once per block so a message-thread
    // resynthesizeClicks() (click sound settings changed) can never
    // invalidate the buffers mid-render.
    const auto normalClick = clickBuffer;
    const auto accentClick = accentClickBuffer;

    const double bpmValue = bpm.load (std::memory_order_acquire);
    if (bpmValue <= 0.0 || currentSampleRate <= 0.0)
        return;

    const double samplesPerBeat = 60.0 / bpmValue * currentSampleRate;
    if (samplesPerBeat <= 0.0)
        return;

    const int numChannels = buffer.getNumChannels();
    if (numChannels <= 0)
        return;

    // A backward jump in the render position means the clock was reset
    // (a new count-in / capture): drop any click that is still ringing
    // out from the old clock so it can't land on the new beat grid.
    if (startPos < lastMetronomeStartPos)
        activeClicks.clear();
    lastMetronomeStartPos = startPos;

    const int64_t endPos = startPos + numSamples;

    // 1. Ring out clicks that started in earlier blocks. A click burst
    //    outlives one block, so the tail must continue here instead of
    //    being truncated at the block boundary — truncation made beats
    //    near the end of a block sound short and quiet, and the long
    //    accents varied the most.
    for (auto& ac : activeClicks)
        renderClickTail (buffer, ac, startPos, endPos, numChannels);

    activeClicks.erase (std::remove_if (activeClicks.begin(), activeClicks.end(),
                                        [](const ActiveClick& ac)
                                        {
                                            return ac.buffer == nullptr
                                                || ac.readPos
                                                    >= static_cast<int> (ac.buffer->size());
                                        }),
                        activeClicks.end());

    if ((normalClick == nullptr || normalClick->empty())
     && (accentClick == nullptr || accentClick->empty()))
        return;

    // Accent the first beat of every bar — beats whose index is a
    // multiple of countInBeats (default 4). Falls back to 4-beat bars
    // when count-in is disabled so the accent still works during plain
    // recording. The accent uses its own brighter, louder click; the
    // other beats use the softer tick.
    const int beatsPerBar = juce::jmax (1, countInBeats.load (std::memory_order_acquire));

    // Beat boundaries that fall inside [startPos, startPos + numSamples).
    const int firstBeat  = static_cast<int> (std::ceil (static_cast<double> (startPos) / samplesPerBeat));
    const int lastBeat   = static_cast<int> (std::floor (static_cast<double> (endPos)   / samplesPerBeat));

    for (int beat = firstBeat; beat <= lastBeat; ++beat)
    {
        const int64_t beatSample = static_cast<int64_t> (beat * samplesPerBeat);
        if (beatSample < startPos || beatSample >= endPos)
            continue;

        const bool isAccent = (beat % beatsPerBar == 0)
            && accentClick != nullptr && ! accentClick->empty();
        if (isAccent     == false
         && (normalClick == nullptr || normalClick->empty()))
            continue;

        // Start the click at the beat. Render the in-block portion now
        // and keep the ActiveClick so following blocks ring out the rest.
        ActiveClick ac;
        ac.buffer     = isAccent ? accentClick : normalClick;
        ac.nextSample = beatSample;
        ac.readPos    = 0;
        renderClickTail (buffer, ac, startPos, endPos, numChannels);
        activeClicks.push_back (ac);
    }
}

void BluePrinterAudioProcessor::renderMidiClockInBlock (juce::MidiBuffer& midiMessages,
                                                        int64_t metronomePos,
                                                        int numSamples)
{
    if (! clockRunning.load (std::memory_order_acquire))
        return;

    const double bpmValue = bpm.load (std::memory_order_acquire);
    if (bpmValue <= 0.0 || currentSampleRate <= 0.0)
        return;

    // 24 MIDI clock pulses per quarter note.
    const double samplesPerClock = (60.0 * currentSampleRate) / (bpmValue * 24.0);
    if (samplesPerClock <= 0.0)
        return;

    const int64_t endPos = metronomePos + numSamples;
    const int64_t firstPulse = static_cast<int64_t> (std::ceil  (static_cast<double> (metronomePos) / samplesPerClock));
    const int64_t lastPulse  = static_cast<int64_t> (std::floor (static_cast<double> (endPos)        / samplesPerClock));

    for (int64_t pulse = firstPulse; pulse <= lastPulse; ++pulse)
    {
        const int64_t pulseSample = static_cast<int64_t> (pulse * samplesPerClock);
        const int offset = static_cast<int> (pulseSample - metronomePos);
        if (offset < 0 || offset >= numSamples)
            continue;

        midiMessages.addEvent (juce::MidiMessage::midiClock(), offset);

        // Also punch out directly for standalone mode so external
        // MIDI hardware receives the clock regardless of host routing.
        {
            const juce::ScopedLock sl (midiOutputLock);
            if (midiOutput != nullptr)
                midiOutput->sendMessageNow (juce::MidiMessage::midiClock());
        }
    }
}

static void sendDirectMidiStart (std::unique_ptr<juce::MidiOutput>& out,
                                 juce::CriticalSection& lock)
{
    juce::ScopedLock sl (lock);
    if (out != nullptr)
        out->sendMessageNow (juce::MidiMessage::midiStart());
}

static void sendDirectMidiStop (std::unique_ptr<juce::MidiOutput>& out,
                                juce::CriticalSection& lock)
{
    juce::ScopedLock sl (lock);
    if (out != nullptr)
        out->sendMessageNow (juce::MidiMessage::midiStop());
}

void BluePrinterAudioProcessor::renderPlayback (juce::AudioBuffer<float>& destination, int numSamples)
{
    int currentId = playingSnippetId.load (std::memory_order_acquire);
    if (currentId < 0)
    {
        playbackActive.store (false, std::memory_order_release);
        return;
    }

    if (playbackSnippet == nullptr || playbackSnippet->id != currentId)
    {
        playbackSnippet = library.findById (currentId);
        playbackReadPos.store (0, std::memory_order_release);
    }

    if (playbackSnippet == nullptr || playbackSnippet->audio == nullptr)
    {
        playbackActive.store (false, std::memory_order_release);
        playingSnippetId.store (-1, std::memory_order_release);
        return;
    }

    const auto& audio = *playbackSnippet->audio;
    auto readPos = static_cast<int> (playbackReadPos.load (std::memory_order_acquire));
    const int totalSamples = audio.getNumSamples();

    if (readPos >= totalSamples)
    {
        playbackActive.store (false, std::memory_order_release);
        playingSnippetId.store (-1, std::memory_order_release);
        playbackReadPos.store (0, std::memory_order_release);
        return;
    }

    const int channels = juce::jmin (destination.getNumChannels(), audio.getNumChannels());
    const int toCopy   = juce::jmin (numSamples, totalSamples - readPos);

    // Non-destructive per-snippet trim, applied before the master Output.
    const float snippetGain = juce::Decibels::decibelsToGain (playbackSnippet->gainDb);
    for (int ch = 0; ch < channels; ++ch)
    {
        destination.copyFrom (ch, 0, audio, ch, readPos, toCopy);
        if (snippetGain != 1.0f)
            destination.applyGain (ch, 0, toCopy, snippetGain);
    }

    // Fill the rest of the buffer with silence if playback ends mid-block.
    if (toCopy < numSamples)
    {
        for (int ch = 0; ch < destination.getNumChannels(); ++ch)
            destination.clear (ch, toCopy, numSamples - toCopy);
    }

    readPos += toCopy;
    playbackReadPos.store (readPos, std::memory_order_release);

    if (readPos >= totalSamples)
    {
        playbackActive.store (false, std::memory_order_release);
        playingSnippetId.store (-1, std::memory_order_release);
        playbackReadPos.store (0, std::memory_order_release);
    }
}

void BluePrinterAudioProcessor::renderTakePlayback (juce::AudioBuffer<float>& destination, int numSamples)
{
    // Review playback of the pending take: a one-shot read of
    // recordBuffer [0, takeLength), rendered like snippet playback
    // (overwrites the output, applies the playback volume).
    const auto length = takeLength.load (std::memory_order_acquire);
    if (recordBuffer == nullptr || length <= 0)
    {
        takePlaybackActive.store (false, std::memory_order_release);
        takePlaybackPos.store (0, std::memory_order_release);
        return;
    }

    auto readPos = static_cast<int> (takePlaybackPos.load (std::memory_order_acquire));
    if (readPos >= length)
    {
        takePlaybackActive.store (false, std::memory_order_release);
        takePlaybackPos.store (0, std::memory_order_release);
        return;
    }

    const int channels = juce::jmin (destination.getNumChannels(), recordBuffer->getNumChannels());
    const int toCopy   = juce::jmin (numSamples, static_cast<int> (length - readPos));

    for (int ch = 0; ch < channels; ++ch)
        destination.copyFrom (ch, 0, *recordBuffer, ch, readPos, toCopy);

    // Fill the rest of the buffer with silence if playback ends mid-block.
    if (toCopy < numSamples)
    {
        for (int ch = 0; ch < destination.getNumChannels(); ++ch)
            destination.clear (ch, toCopy, numSamples - toCopy);
    }

    readPos += toCopy;
    takePlaybackPos.store (readPos, std::memory_order_release);

    if (readPos >= length)
    {
        takePlaybackActive.store (false, std::memory_order_release);
        takePlaybackPos.store (0, std::memory_order_release);
    }
}

void BluePrinterAudioProcessor::computeLevelsInto (const juce::AudioBuffer<float>& source,
                                                   int numSamples,
                                                   std::atomic<float>& levelAtomic,
                                                   std::atomic<float>& peakAtomic,
                                                   float gain,
                                                   std::atomic<bool>* clipAtomic)
{
    if (numSamples <= 0)
        return;

    float peak = 0.0f;
    double sumSquares = 0.0;
    int countedSamples = 0;

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        const float* data = source.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = data[i];
            const float a = std::abs (v);
            if (a > peak)
                peak = a;
            sumSquares += static_cast<double> (v) * static_cast<double> (v);
            ++countedSamples;
        }
    }

    const double rms = countedSamples > 0
        ? std::sqrt (sumSquares / static_cast<double> (countedSamples))
        : 0.0;

    const float prevLevel = levelAtomic.load (std::memory_order_acquire);
    const float prevPeak  = peakAtomic.load  (std::memory_order_acquire);
    const float alpha     = 1.0f / static_cast<float> (levelSmoothing);
    const float newLevel  = prevLevel + (static_cast<float> (rms) * gain - prevLevel) * alpha;
    const float decayPeak = prevPeak * 0.95f;

    levelAtomic.store (newLevel, std::memory_order_release);
    peakAtomic.store  (juce::jmax (peak * gain, decayPeak), std::memory_order_release);

    if (clipAtomic != nullptr && peak * gain >= 1.0f)
        clipAtomic->store (true, std::memory_order_release);
}

void BluePrinterAudioProcessor::computeLevels (const juce::AudioBuffer<float>& source, int numSamples)
{
    computeLevelsInto (source, numSamples, inputLevel, inputPeak, 1.0f, &inputClipped);
}

void BluePrinterAudioProcessor::resetClip (const juce::String& target)
{
    const bool all = target == "all";
    if (all || target == "input")  inputClipped.store  (false, std::memory_order_release);
    if (all || target == "record") recordClipped.store (false, std::memory_order_release);
    if (all || target == "output") outputClipped.store (false, std::memory_order_release);
    if (all || target == "loop")   loopPlayClipped.store (false, std::memory_order_release);
}

//==============================================================================
void BluePrinterAudioProcessor::startRecording()
{
    if (recordingRequested.load (std::memory_order_acquire)
        || preRollActive.load (std::memory_order_acquire))
        return;

    if (playbackActive.load (std::memory_order_acquire))
        stopPlayback();

    if (recordBuffer == nullptr || maxRecordSamples <= 0)
        return;

    // The looper and the take recorder share recordBuffer; don't let
    // them capture simultaneously.
    if (looperCaptureArmed.load (std::memory_order_acquire)
        || looperPreRollActive.load (std::memory_order_acquire))
        setLooperRecording (false);

    const int beats = countInBeats.load (std::memory_order_acquire);
    metronomePosition.store (0, std::memory_order_release);

    if (metronomeEnabled.load (std::memory_order_acquire) && beats > 0)
    {
        // Count-in: play N beats of click, then start recording. The
        // transition to actual recording happens in processBlock.
        transportPosition.store (0, std::memory_order_release);
        preRollActive.store (true, std::memory_order_release);
        recordingState.store (RecordingState::Recording, std::memory_order_release);
        recordingFinalizePending.store (false, std::memory_order_release);
    }
    else
    {
        beginActualRecording();
    }

    // The take may drive the MIDI clock: start it now so external gear
    // syncs from the first count-in beat (or from recording start when
    // there is no count-in).
    updateClockRunState();

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setLooperRecording (bool enabled)
{
    if (enabled)
    {
        // The looper and the take recorder share recordBuffer; don't let
        // them capture simultaneously.
        if (recordingRequested.load (std::memory_order_acquire)
            || preRollActive.load (std::memory_order_acquire))
            stopRecording();

        if (recordBuffer == nullptr || maxRecordSamples <= 0)
            return;

        // Any new capture (take or loop) invalidates the pending take.
        clearPendingTake();

        const bool overdubbing = looperOverdub.load (std::memory_order_acquire)
            && looperLooping.load (std::memory_order_acquire)
            && audioLoopLength.load (std::memory_order_acquire) > 0;

        // Whether a count-in will run. During an overdub count-in the loop
        // stays silent and is started from the top only when capture begins,
        // so the new layer lines up with the loop downbeat.
        const bool countIn = metronomeEnabled.load (std::memory_order_acquire)
            && looperCountInBeats.load (std::memory_order_acquire) > 0;

        if (overdubbing)
        {
            // Layer over the existing loop: keep the loop intact and
            // write the new input into the region after it (the audio
            // thread taps overdubWritePos while audioLoopLength stays
            // fixed, so the wrap boundary never moves). The layer is
            // mixed into the loop on stop.
            looperPreRollActive.store (false, std::memory_order_release);
            looperCaptureArmed.store (false, std::memory_order_release);
            audioLoopRecording.store (false, std::memory_order_release);
            looperOverdubCapture.store (true, std::memory_order_release);
            // Write the layer after the FULL loop, not after the cropped
            // window: with a start crop the window ends at
            // audioLoopStart + audioLoopLength > audioLoopLength, so a
            // layer based at audioLoopLength would overwrite the loop's
            // tail.
            overdubWritePos.store (audioLoopFullLength.load (std::memory_order_acquire),
                                   std::memory_order_release);
            audioLoopPosition.store (0, std::memory_order_release);
            // With a count-in, hold the loop silent through it and start it
            // from position 0 when capture arms (processBlock step 8).
            audioLoopPlaying.store (! countIn, std::memory_order_release);
        }
        else
        {
            // Fresh capture: wipe the loop and start from sample 0.
            looperPreRollActive.store (false, std::memory_order_release);
            looperCaptureArmed.store (false, std::memory_order_release);
            audioLoopRecording.store (false, std::memory_order_release);
            audioLoopPlaying.store (false, std::memory_order_release);
            looperOverdubCapture.store (false, std::memory_order_release);
            overdubWritePos.store (0, std::memory_order_release);
            audioLoopStart.store (0, std::memory_order_release);
            audioLoopLength.store (0, std::memory_order_release);
            audioLoopFullLength.store (0, std::memory_order_release);
            audioLoopPosition.store (0, std::memory_order_release);
            looperCropStartBeats = 0;
            looperCropEndBeats = 0;
            looperPeaks.clear();
        }

        if (countIn)
        {
            // Count-in: play N beats of click, then start capture. The
            // transition happens in processBlock.
            metronomePosition.store (0, std::memory_order_release);
            transportPosition.store (0, std::memory_order_release);
            looperPreRollActive.store (true, std::memory_order_release);
        }
        else
        {
            looperCaptureArmed.store (true, std::memory_order_release);
            audioLoopRecording.store (true, std::memory_order_release);
            // Capture is starting without a count-in: re-sync external
            // gear if the clock is already running (the header-level
            // clock toggle).
            if (clockRunning.load (std::memory_order_acquire))
                midiStartPending.store (true, std::memory_order_release);
        }
    }
    else
    {
        looperPreRollActive.store (false, std::memory_order_release);
        looperCaptureArmed.store (false, std::memory_order_release);
        audioLoopRecording.store (false, std::memory_order_release);

        // Overdub stop: stop the loop playback first (so the mix below
        // can't race the audio thread's unlocked loop reads), then mix
        // the recorded layer into the loop and refresh the waveform.
        // The loop is left stopped — press Play to hear the result.
        if (looperOverdubCapture.exchange (false, std::memory_order_acq_rel))
        {
            audioLoopPlaying.store (false, std::memory_order_release);
            const auto loopLength = audioLoopLength.load (std::memory_order_acquire);
            const auto layerBase  = audioLoopFullLength.load (std::memory_order_acquire);
            const auto layerLength = overdubWritePos.load (std::memory_order_acquire) - layerBase;
            mixOverdubLayer (loopLength, layerBase, layerLength);
            refreshLooperPeaks();
        }
        else
        {
            trimLooperToMusicalGrid();
        }
    }

    // The looper may drive the MIDI clock: start it when capture begins
    // (with or without count-in — the pre-roll starts the clock so the
    // drum machine is synced by the first capture beat) and stop it when
    // capture ends.
    updateClockRunState();

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::refreshLooperPeaks()
{
    looperPeaks.clear();

    const auto full = audioLoopFullLength.load (std::memory_order_acquire);
    if (recordBuffer == nullptr || full <= 0)
        return;

    // Peaks cover the FULL loop (crop regions included) so the UI's crop
    // shading can grey the cropped beat ranges over the actual audio.
    juce::AudioBuffer<float> region (recordBuffer->getNumChannels(), static_cast<int> (full));
    {
        const juce::ScopedLock sl (recordLock);
        for (int ch = 0; ch < region.getNumChannels(); ++ch)
            region.copyFrom (ch, 0, *recordBuffer, ch, 0, static_cast<int> (full));
    }
    looperPeaks = SnippetLibrary::computePeaks (region, 256);
}

void BluePrinterAudioProcessor::trimLooperToMusicalGrid()
{
    const auto captured = audioLoopLength.load (std::memory_order_acquire);
    if (captured <= 0 || currentSampleRate <= 0.0)
        return;

    const auto beat = 60.0 * currentSampleRate / juce::jmax (1.0f, bpm.load());
    const auto bar = beat * 4.0;
    // Snap to a whole number of bars so the loop's downbeat stays on the
    // beat grid on every cycle. The old code clamped the snapped length
    // back to the raw capture (`jlimit(1, captured, target)`), so a
    // capture that stopped a little short of a bar boundary kept its
    // non-grid length and the loop's downbeat drifted against the click
    // on every wrap — heard as the second cycle starting late. The loop
    // is now exactly the snapped length: audio past the capture is
    // truncated, and a short capture is zero-padded so the grid boundary
    // is preserved.
    auto target = static_cast<int64_t> (std::llround (static_cast<double> (captured) / bar) * bar);
    if (target <= 0)
        target = static_cast<int64_t> (std::llround (static_cast<double> (captured) / beat) * beat);
    target = juce::jlimit<int64_t> (1, static_cast<int64_t> (maxRecordSamples), target);

    if (recordBuffer != nullptr && target > captured)
    {
        // recordBuffer may still hold a previous take's audio past the
        // capture region, so explicitly silence the padded tail.
        const juce::ScopedLock sl (recordLock);
        for (int ch = 0; ch < recordBuffer->getNumChannels(); ++ch)
            recordBuffer->clear (ch, static_cast<int> (captured),
                                 static_cast<int> (target - captured));
    }

    audioLoopStart.store (0, std::memory_order_release);
    audioLoopLength.store (target, std::memory_order_release);
    // The grid-trimmed capture is the reference every future crop is
    // measured against (see setLoopCrop) so crop changes stay reversible.
    audioLoopFullLength.store (target, std::memory_order_release);
    looperCropStartBeats = 0;
    looperCropEndBeats = 0;
    refreshLooperPeaks();
}

// Mixes the overdub layer (recorded into [loopLength, loopLength +
// layerLength) during the capture) into the audible loop window
// [audioLoopStart, audioLoopStart + loopLength), wrapping across loop
// cycles pedal-style — play past the end and it layers on top of the
// next cycle. Message thread only: capture is stopped and playback is
// off, so the only recordBuffer access here is ours, under the lock
// (matching refreshLooperPeaks).
void BluePrinterAudioProcessor::mixOverdubLayer (int64_t loopLength, int64_t layerBase,
                                                 int64_t layerLength)
{
    if (recordBuffer == nullptr || loopLength <= 0 || layerLength <= 0
        || layerBase < 0 || layerBase + layerLength > recordBuffer->getNumSamples())
        return;

    const auto start = audioLoopStart.load (std::memory_order_acquire);
    const int channels = recordBuffer->getNumChannels();
    // Overdub trim: attenuate each new layer before it piles into the
    // loop (0 dB is a no-op).
    const float layerGain = juce::Decibels::decibelsToGain (
        overdubLevel.load (std::memory_order_acquire));

    const juce::ScopedLock sl (recordLock);
    for (int64_t offset = 0; offset < layerLength;)
    {
        const auto cyclePos = offset % loopLength;
        const auto toMix = juce::jmin (loopLength - cyclePos, layerLength - offset);
        for (int ch = 0; ch < channels; ++ch)
            recordBuffer->addFrom (ch, static_cast<int> (start + cyclePos),
                                   *recordBuffer, ch,
                                   static_cast<int> (layerBase + offset),
                                   static_cast<int> (toMix), layerGain);
        offset += toMix;
    }

    // The mixed loop can now exceed 0 dBFS even if each layer was
    // trimmed — latch the loop clip indicator so the UI shows it.
    float peak = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
        peak = juce::jmax (peak, recordBuffer->getMagnitude (
            ch, static_cast<int> (start), static_cast<int> (loopLength)));
    if (peak >= 1.0f)
        loopPlayClipped.store (true, std::memory_order_release);
}

int BluePrinterAudioProcessor::saveLoopSnippet()
{
    const auto start = audioLoopStart.load (std::memory_order_acquire);
    const auto captured = audioLoopLength.load (std::memory_order_acquire);
    if (recordBuffer == nullptr || captured <= 0)
        return -1;

    std::shared_ptr<Snippet> snippet;

    {
        // The audio thread only writes the loop while capture is armed,
        // so a message-thread read here is safe once capture has stopped.
        const juce::ScopedLock sl (recordLock);
        if (looperCaptureArmed.load (std::memory_order_acquire)
            || looperPreRollActive.load (std::memory_order_acquire))
            return -1;

        const int channels = recordBuffer->getNumChannels();
        auto snippetBuffer = std::make_shared<juce::AudioBuffer<float>> (channels, static_cast<int> (captured));
        for (int ch = 0; ch < channels; ++ch)
            snippetBuffer->copyFrom (ch, 0, *recordBuffer, ch, static_cast<int> (start), static_cast<int> (captured));

        auto defaultName = "Loop " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");
        snippet = library.addSnippet (snippetBuffer, getSampleRate(), defaultName);
    }

    if (snippet == nullptr)
        return -1;

    listeners.call ([](Listener& l) { l.libraryChanged(); });

    // One-click save to the library folder, like the take recorder. No
    // folder set = the snippet stays in the in-memory library only.
    juce::File folder;
    {
        juce::ScopedLock lock (libraryFolderLock);
        folder = libraryFolder;
    }
    if (folder.isDirectory())
    {
        juce::String outPath, outError;
        if (library.saveSnippetToFolder (*snippet, folder, outPath, outError))
            library.markSaved (snippet->id, outPath);
        else
        {
            juce::ScopedLock lock (libraryFolderLock);
            lastSaveError = outError;
            listeners.call ([](Listener& l) { l.libraryChanged(); });
        }
    }

    return snippet->id;
}

void BluePrinterAudioProcessor::setLooperPlaying (bool enabled)
{
    // Loop playback supersedes take-review playback.
    if (enabled && takePlaybackActive.load (std::memory_order_acquire))
    {
        takePlaybackActive.store (false, std::memory_order_release);
        takePlaybackPos.store (0, std::memory_order_release);
    }

    audioLoopPosition.store (0, std::memory_order_release);
    audioLoopPlaying.store (enabled && audioLoopLength.load (std::memory_order_acquire) > 0,
                            std::memory_order_release);
    updateClockRunState();
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setLooperLooping (bool enabled)
{
    looperLooping.store (enabled);
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

// Overdub mode (session-only, like looperLooping): with a loop captured
// and looping on, record layers the new input over the loop instead of
// replacing it. Takes effect on the next capture.
void BluePrinterAudioProcessor::setLooperOverdub (bool enabled)
{
    looperOverdub.store (enabled, std::memory_order_release);
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setLooperCountInBeats (int beats)
{
    looperCountInBeats.store (juce::jlimit (0, 8, beats));
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setLoopCrop (int startBeats, int endBeats)
{
    const auto full = audioLoopFullLength.load (std::memory_order_acquire);
    if (full <= 0 || currentSampleRate <= 0.0)
        return;

    const auto beat = 60.0 * currentSampleRate / juce::jmax (1.0f, bpm.load());
    // Total beats of the FULL loop — crops are measured against this, never
    // against the already-cropped window, so moving a crop handle back
    // toward 0 restores the region it cut off (the old code re-derived the
    // total from the shrunken window and every adjustment deleted more of
    // the take).
    const auto loopBeats = juce::jmax (1, static_cast<int> (std::llround (static_cast<double> (full) / beat)));

    startBeats = juce::jlimit (0, loopBeats - 1, startBeats);
    endBeats = juce::jlimit (0, loopBeats - 1 - startBeats, endBeats);
    looperCropStartBeats = startBeats;
    looperCropEndBeats = endBeats;

    const auto trimStart = static_cast<int64_t> (startBeats * beat);
    const auto trimEnd = static_cast<int64_t> (endBeats * beat);
    audioLoopStart.store (trimStart, std::memory_order_release);
    audioLoopLength.store (full - trimStart - trimEnd, std::memory_order_release);

    // Keep the playhead inside the cropped window.
    const auto remaining = audioLoopLength.load (std::memory_order_acquire);
    audioLoopPosition.store (juce::jmin (audioLoopPosition.load (std::memory_order_acquire),
                                         juce::jmax<int64_t> (0, remaining - 1)),
                             std::memory_order_release);
    if (remaining <= 0)
        audioLoopPlaying.store (false, std::memory_order_release);

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::clearLoop()
{
    looperPreRollActive.store (false, std::memory_order_release);
    looperCaptureArmed.store (false, std::memory_order_release);
    audioLoopRecording.store (false, std::memory_order_release);
    looperOverdubCapture.store (false, std::memory_order_release);
    overdubWritePos.store (0, std::memory_order_release);
    audioLoopPlaying.store (false, std::memory_order_release);
    audioLoopStart.store (0, std::memory_order_release);
    audioLoopLength.store (0, std::memory_order_release);
    audioLoopFullLength.store (0, std::memory_order_release);
    audioLoopPosition.store (0, std::memory_order_release);
    looperCropStartBeats = 0;
    looperCropEndBeats = 0;
    looperPeaks.clear();
    updateClockRunState();
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::beginActualRecording()
{
    // A new take wipes recordBuffer, so any pending (unsaved) take is
    // invalidated. Audio-thread safe: atomics only — takePeaks stays
    // stale until the next finalize rebuilds it.
    takePending.store (false, std::memory_order_release);
    takePlaybackActive.store (false, std::memory_order_release);
    takePlaybackPos.store (0, std::memory_order_release);
    takeLength.store (0, std::memory_order_release);

    {
        const juce::ScopedLock sl (recordLock);
        recordBuffer->clear();
        recordWritePos.store (0, std::memory_order_release);
        recordingState.store (RecordingState::Recording, std::memory_order_release);
        recordingFinalizePending.store (false, std::memory_order_release);
    }
    recordingRequested.store (true, std::memory_order_release);
    transportPosition.store (0, std::memory_order_release);

    // Re-sync external gear when the clock is already running (the
    // header-level clock toggle): fire Start again so the drum machine
    // restarts its pattern on the take's first beat. When the clock is
    // off, nothing to re-sync — the take records without one.
    if (clockRunning.load (std::memory_order_acquire))
    {
        midiStartPending.store (true, std::memory_order_release);
        sendDirectMidiStart (midiOutput, midiOutputLock);
    }
}

void BluePrinterAudioProcessor::stopRecording()
{
    // Cancel count-in if one is running. Nothing was recorded.
    if (preRollActive.load (std::memory_order_acquire))
    {
        preRollActive.store (false, std::memory_order_release);
        transportPosition.store (0, std::memory_order_release);
        updateClockRunState();

        if (recordingState.load() != RecordingState::Idle)
        {
            recordingState.store (RecordingState::Idle, std::memory_order_release);
            listeners.call ([](Listener& l) { l.transportChanged(); });
        }
        return;
    }

    if (! recordingRequested.load (std::memory_order_acquire)
        && ! recordingFinalizePending.load (std::memory_order_acquire))
    {
        if (recordingState.load() != RecordingState::Idle)
        {
            recordingState.store (RecordingState::Idle, std::memory_order_release);
            listeners.call ([](Listener& l) { l.transportChanged(); });
        }
        return;
    }

    recordingRequested.store (false, std::memory_order_release);
    recordingState.store (RecordingState::Idle, std::memory_order_release);
    transportPosition.store (0, std::memory_order_release);
    recordingFinalizePending.store (true, std::memory_order_release);

    // The take no longer drives the clock: Stop unless another source
    // (global toggle, looper) keeps it running.
    updateClockRunState();

    // The message thread is finalising as fast as possible so the snippet
    // appears without waiting for the next transport-timer tick.
    finalizeRecordingOnMessageThread();

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::startPlayback (int snippetId)
{
    if (snippetId < 0)
        return;

    if (recordingRequested.load (std::memory_order_acquire))
        stopRecording();

    // Snippet playback supersedes take-review playback.
    if (takePlaybackActive.load (std::memory_order_acquire))
    {
        takePlaybackActive.store (false, std::memory_order_release);
        takePlaybackPos.store (0, std::memory_order_release);
    }

    if (library.indexOfId (snippetId) < 0)
        return;

    playbackReadPos.store (0, std::memory_order_release);
    playingSnippetId.store (snippetId, std::memory_order_release);
    playbackActive.store (true, std::memory_order_release);
    playbackSnippet.reset();

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::stopPlayback()
{
    if (! playbackActive.load (std::memory_order_acquire)
        && playingSnippetId.load (std::memory_order_acquire) < 0)
        return;

    playbackActive.store (false, std::memory_order_release);
    playingSnippetId.store (-1, std::memory_order_release);
    playbackReadPos.store (0, std::memory_order_release);
    playbackSnippet.reset();

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

bool BluePrinterAudioProcessor::deleteSnippet (int id)
{
    // Hold a strong ref to the snippet so its data is still valid when we
    // remove the files from disk after the library forgets about it.
    auto snippet = library.findById (id);

    const bool removed = library.removeSnippet (id);
    if (removed)
    {
        if (playingSnippetId.load() == id)
            stopPlayback();

        if (snippet != nullptr)
            SnippetLibrary::deleteSavedFiles (*snippet);

        listeners.call ([](Listener& l) { l.libraryChanged(); l.transportChanged(); });
    }
    return removed;
}

bool BluePrinterAudioProcessor::updateSnippetMeta (int id, const juce::String& name, const juce::String& comments)
{
    const bool ok = library.updateMeta (id, name, comments);
    if (ok)
    {
        // Persist the change to the sidecar JSON so the edit survives a
        // reload of the library folder.
        const bool persisted = library.persistMetadata (id);
        if (! persisted)
        {
            juce::ScopedLock lock (libraryFolderLock);
            lastSaveError = "Could not save metadata to disk. Make sure a library folder is set.";
        }
        listeners.call ([](Listener& l) { l.libraryChanged(); });
        return persisted;
    }
    return false;
}

bool BluePrinterAudioProcessor::setSnippetColor (int id, const juce::String& color)
{
    const bool ok = library.updateColor (id, color);
    if (ok)
    {
        // Persist the change to the sidecar JSON so the tag survives a
        // reload of the library folder.
        const bool persisted = library.persistMetadata (id);
        if (! persisted)
        {
            juce::ScopedLock lock (libraryFolderLock);
            lastSaveError = "Could not save metadata to disk. Make sure a library folder is set.";
        }
        listeners.call ([](Listener& l) { l.libraryChanged(); });
        return persisted;
    }
    return false;
}

bool BluePrinterAudioProcessor::setSnippetGain (int id, float gainDb)
{
    if (! library.updateGain (id, gainDb))
        return false;

    // Defer the sidecar write and the library snapshot: the Gain knob
    // emits on every pointer move, and rebuilding the whole snippets
    // payload per tick is what makes such knobs laggy. The in-memory
    // value is correct immediately.
    snippetGainPersistPending = true;
    snippetGainPersistId = id;
    snippetGainPersistDeadline = juce::Time::currentTimeMillis() + 500;
    return true;
}

void BluePrinterAudioProcessor::flushSnippetGainPersist()
{
    if (! snippetGainPersistPending)
        return;

    snippetGainPersistPending = false;
    if (! library.persistMetadata (snippetGainPersistId))
    {
        juce::ScopedLock lock (libraryFolderLock);
        lastSaveError = "Could not save metadata to disk. Make sure a library folder is set.";
    }
    listeners.call ([](Listener& l) { l.libraryChanged(); });
}

bool BluePrinterAudioProcessor::normalizeSnippet (int id)
{
    auto snippet = library.findById (id);
    if (snippet == nullptr || snippet->audio == nullptr)
        return false;

    const auto& audio = *snippet->audio;
    float peak = 0.0f;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        peak = juce::jmax (peak, audio.getMagnitude (ch, 0, audio.getNumSamples()));

    if (peak <= 0.0f)
        return false;

    // Bring the peak to -1 dBFS: gainDb = targetDb - peakDb.
    const float gainDb = -1.0f - juce::Decibels::gainToDecibels (peak, -144.0f);
    if (! setSnippetGain (id, gainDb))
        return false;

    // Normalize is a one-shot user action: persist + refresh now rather
    // than waiting for the debounce.
    flushSnippetGainPersist();
    return true;
}

void BluePrinterAudioProcessor::detectSnippetKeyAndNotes (int id)
{
    // Hold a strong ref to the snippet's audio so the worker thread
    // can run KeyDetector on it without racing against a later
    // deleteSnippet. The actual snippet mutation happens back on the
    // message thread inside the callAsync below.
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    double sampleRate = 0.0;
    {
        auto snippet = library.findById (id);
        if (snippet == nullptr || snippet->audio == nullptr)
            return;
        audio = snippet->audio;
        sampleRate = snippet->sampleRate;
    }

    // Clear the existing key/notes immediately so the UI flips into the
    // "detecting" state without waiting for the worker. If the worker
    // later reports nothing useful, the snippet stays empty.
    if (auto snippet = library.findById (id))
    {
        snippet->key.clear();
        snippet->keyConfidence = 0.0f;
        snippet->detectedNotes.clear();
        listeners.call ([](Listener& l) { l.libraryChanged(); });
    }

    // The detection walks the entire audio buffer and does a 4096-point
    // FFT per frame, so it can take a few hundred ms on a long
    // snippet. Run it on a worker thread and post the result back.
    std::thread ([this, id, audio, sampleRate]()
    {
        const KeyDetectionResult result = KeyDetector::detectKey (*audio, sampleRate);
        juce::MessageManager::callAsync ([this, id, result]()
        {
            auto snippet = library.findById (id);
            if (snippet == nullptr)
                return; // snippet was deleted while we were analysing
            snippet->key            = result.key;
            snippet->keyConfidence  = result.confidence;
            snippet->detectedNotes  = result.detectedNotes;
            if (! library.persistMetadata (id))
            {
                juce::ScopedLock lock (libraryFolderLock);
                lastSaveError = "Key detection result could not be saved to disk. Make sure a library folder is set and the file is writable.";
            }
            listeners.call ([](Listener& l) { l.libraryChanged(); });
        });
    }).detach();
}

juce::String BluePrinterAudioProcessor::getLibraryFolder() const
{
    juce::ScopedLock lock (libraryFolderLock);
    return libraryFolder.getFullPathName();
}

void BluePrinterAudioProcessor::setLibraryFolder (const juce::File& folder)
{
    {
        juce::ScopedLock lock (libraryFolderLock);
        libraryFolder = folder;
    }

    // Persist the new folder so the standalone remembers it on next
    // launch. Done before loadFromFolder so a crash mid-load still
    // leaves the folder choice saved. Must be called outside the lock
    // because persistLibraryFolder re-acquires it (CriticalSection is
    // non-reentrant).
    persistLibraryFolder();

    // Pull any pre-existing recordings from the folder so the user can
    // listen to, edit, or delete them. Skips files that are already loaded.
    if (folder.isDirectory())
    {
        juce::String loadError;
        library.loadFromFolder (folder, loadError);
    }

    listeners.call ([](Listener& l) { l.libraryChanged(); });
}

void BluePrinterAudioProcessor::refreshLibraryFromFolder()
{
    juce::File folder;
    {
        juce::ScopedLock lock (libraryFolderLock);
        folder = libraryFolder;
    }

    if (folder.isDirectory())
    {
        juce::String loadError;
        library.loadFromFolder (folder, loadError);
    }

    listeners.call ([](Listener& l) { l.libraryChanged(); });
}

void BluePrinterAudioProcessor::setMetronomeEnabled (bool enabled)
{
    if (metronomeEnabled.load (std::memory_order_acquire) == enabled)
        return;
    metronomeEnabled.store (enabled, std::memory_order_release);
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setBpm (float newBpm)
{
    const float clamped = juce::jlimit (20.0f, 300.0f, newBpm);
    if (juce::approximatelyEqual (bpm.load (std::memory_order_acquire), clamped))
        return;
    bpm.store (clamped, std::memory_order_release);
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setCountInBeats (int beats)
{
    const int clamped = juce::jlimit (0, 16, beats);
    if (countInBeats.load (std::memory_order_acquire) == clamped)
        return;
    countInBeats.store (clamped, std::memory_order_release);
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setLoopLevel (float levelDb)
{
    const float clamped = juce::jlimit (-60.0f, 12.0f, levelDb);
    if (loopLevel.load (std::memory_order_acquire) == clamped)
        return;
    loopLevel.store (clamped, std::memory_order_release);
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setDryLevel (float levelDb)
{
    const float clamped = juce::jlimit (-60.0f, 0.0f, levelDb);
    if (dryLevel.load (std::memory_order_acquire) == clamped)
        return;
    dryLevel.store (clamped, std::memory_order_release);
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setOverdubLevel (float levelDb)
{
    const float clamped = juce::jlimit (-60.0f, 0.0f, levelDb);
    if (overdubLevel.load (std::memory_order_acquire) == clamped)
        return;
    overdubLevel.store (clamped, std::memory_order_release);
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setClickParams (float pitch, float accentPitch,
                                                float decay, float volume,
                                                float accentVolume, float noise)
{
    clickPitch        = juce::jlimit (400.0f, 3000.0f, pitch);
    clickAccentPitch  = juce::jlimit (400.0f, 3000.0f, accentPitch);
    clickDecay        = juce::jlimit (20.0f, 300.0f, decay);
    clickVolume       = juce::jlimit (0.0f, 1.0f, volume);
    clickAccentVolume = juce::jlimit (0.0f, 1.0f, accentVolume);
    clickNoise        = juce::jlimit (0.0f, 0.3f, noise);

    resynthesizeClicks();
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::resynthesizeClicks()
{
    const double sampleRate = currentSampleRate;
    if (sampleRate <= 0.0)
        return;

    struct ClickParams
    {
        double fundamental;
        double decayRate;
        double duration;
        float  amplitude;
        float  noiseLevel;
    };

    auto makeClick = [sampleRate](const ClickParams& p) -> std::vector<float>
    {
        const int n = juce::jmax (1, static_cast<int> (sampleRate * p.duration));
        std::vector<float> buf (static_cast<size_t> (n));

        const int attackSamples = juce::jmax (1, static_cast<int> (sampleRate * 0.002));
        const int fadeSamples   = juce::jmax (1, static_cast<int> (sampleRate * 0.002));
        const double noiseWindow = 0.004;

        // Deterministic LCG so the click is identical every launch.
        uint32_t noiseState = 0x1B3F5A91u;
        auto nextNoise = [&noiseState]()
        {
            noiseState = noiseState * 1664525u + 1013904223u;
            return (static_cast<float> (noiseState) / static_cast<float> (0xFFFFFFFFu)) * 2.0f - 1.0f;
        };

        const float twoPi = juce::MathConstants<float>::twoPi;
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / sampleRate;

            double env = std::exp (-p.decayRate * t);
            if (i < attackSamples)
                env *= static_cast<double> (i) / attackSamples;
            const int tailLeft = n - i;
            if (tailLeft < fadeSamples)
                env *= static_cast<double> (tailLeft) / fadeSamples;

            const float f = static_cast<float> (t);
            const float tonal = std::sin (twoPi * static_cast<float> (p.fundamental) * f) * 0.55f
                              + std::sin (twoPi * static_cast<float> (p.fundamental * 2.0) * f) * 0.30f
                              + std::sin (twoPi * static_cast<float> (p.fundamental * 3.0) * f) * 0.15f;
            const float noise = t < noiseWindow ? nextNoise() * p.noiseLevel : 0.0f;

            buf[static_cast<size_t> (i)] = (tonal * p.amplitude + noise) * static_cast<float> (env);
        }
        return buf;
    };

    // The accent decays a little slower than the tick so it rings
    // slightly longer, and both get a touch of the same onset noise.
    clickBuffer       = std::make_shared<const std::vector<float>> (
        makeClick ({ static_cast<double> (clickPitch),
                     static_cast<double> (clickDecay),
                     0.040, clickVolume, clickNoise }));

    accentClickBuffer = std::make_shared<const std::vector<float>> (
        makeClick ({ static_cast<double> (clickAccentPitch),
                     static_cast<double> (clickDecay) * 0.78,
                     0.055, clickAccentVolume, clickNoise }));
}

// -------------------------------------------------------------------------
// MIDI clock output — syncs external drum machines / sequencers
// -------------------------------------------------------------------------

void BluePrinterAudioProcessor::setMidiClockEnabled (bool enabled)
{
    const bool prev = midiClockEnabled.exchange (enabled, std::memory_order_release);
    if (enabled == prev)
        return;

    // The header-level toggle is the master clock source: on = the clock
    // runs (free-running, or recording-only when midiClockOnRecord is
    // on), off = stopped.
    updateClockRunState();

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

// "Clock: on record" — restrict the clock to take / loop captures
// instead of free-running. Changing the mode re-arbitrates the run
// condition immediately: turning it on while idle stops the clock;
// turning it off while idle starts the free-run.
void BluePrinterAudioProcessor::setMidiClockOnRecord (bool enabled)
{
    const bool prev = midiClockOnRecord.exchange (enabled, std::memory_order_release);
    if (enabled == prev)
        return;

    updateClockRunState();

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

// Atomics-only (safe on both the audio and the message thread). The
// clock runs when the master toggle is on, and either the mode is
// free-run or a take / loop capture is in progress (including its
// count-in pre-roll, so external gear is synced from the first click).
bool BluePrinterAudioProcessor::wantsClockRun() const
{
    if (! midiClockEnabled.load (std::memory_order_acquire))
        return false;

    if (! midiClockOnRecord.load (std::memory_order_acquire))
        return true;

    return recordingRequested.load (std::memory_order_acquire)
        || preRollActive.load (std::memory_order_acquire)
        || looperCaptureArmed.load (std::memory_order_acquire)
        || looperPreRollActive.load (std::memory_order_acquire);
}

// The clock runs while wantsClockRun() is true. Edges send Start / Stop
// directly to the hardware and queue them for the host buffer. (Takes
// and loop captures ride the clock and re-sync with a Start at
// actual-recording/capture time — see beginActualRecording.)
void BluePrinterAudioProcessor::updateClockRunState()
{
    const bool wantRun = wantsClockRun();

    const bool wasRunning = clockRunning.exchange (wantRun, std::memory_order_acq_rel);

    if (wantRun && ! wasRunning)
    {
        // Rising edge: make sure the output device is open (message
        // thread only) and fire Start so external gear syncs.
        openMidiOutputDevice();
        midiStartPending.store (true, std::memory_order_release);
        sendDirectMidiStart (midiOutput, midiOutputLock);
    }
    else if (! wantRun && wasRunning)
    {
        midiStopPending.store (true, std::memory_order_release);
        sendDirectMidiStop (midiOutput, midiOutputLock);
        closeMidiOutputDevice();
    }
}

void BluePrinterAudioProcessor::refreshClockRunning()
{
    // Message-thread toggle changes already land in clockRunning via
    // updateClockRunState; this mirrors the flag per block so a direct
    // store from a state restore is picked up without waiting for the
    // next user mutation, and commands Start/Stop on the edge (the
    // device open/close itself stays on the message thread).
    const bool wantRun = wantsClockRun();
    const bool wasRunning = clockRunning.exchange (wantRun, std::memory_order_acq_rel);
    if (wantRun == wasRunning)
        return;

    if (wantRun)
    {
        midiStartPending.store (true, std::memory_order_release);
        sendDirectMidiStart (midiOutput, midiOutputLock);
    }
    else
    {
        midiStopPending.store (true, std::memory_order_release);
        sendDirectMidiStop (midiOutput, midiOutputLock);
    }
}

void BluePrinterAudioProcessor::setClickDuringCapture (bool enabled)
{
    clickDuringCapture.store (enabled, std::memory_order_release);
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

juce::String BluePrinterAudioProcessor::getMidiOutputDeviceName() const
{
    juce::ScopedLock sl (midiOutputLock);
    return midiOutputDeviceName;
}

void BluePrinterAudioProcessor::setMidiOutputDeviceName (const juce::String& name)
{
    {
        juce::ScopedLock sl (midiOutputLock);
        if (midiOutputDeviceName == name)
            return;
        midiOutputDeviceName = name;
    }

    closeMidiOutputDevice();
    if (clockRunning.load (std::memory_order_acquire))
        openMidiOutputDevice();
}

void BluePrinterAudioProcessor::openMidiOutputDevice()
{
    const juce::ScopedLock sl (midiOutputLock);

    const auto devices = juce::MidiOutput::getAvailableDevices();
    DBG ("[BluePrinter] MIDI: " << devices.size() << " output device(s) available");
    for (int i = 0; i < devices.size(); ++i)
        DBG ("  [" << i << "] " << devices[i].name);

    if (devices.isEmpty())
    {
        DBG ("[BluePrinter] MIDI: no output devices found — USB drum machine "
             "may need to be connected before launching the app");
        return;
    }

    int index = 0;
    if (midiOutputDeviceName.isNotEmpty())
    {
        for (int i = 0; i < devices.size(); ++i)
        {
            if (devices[i].name == midiOutputDeviceName)
            {
                index = i;
                break;
            }
        }
    }

    DBG ("[BluePrinter] MIDI: trying to open device #" << index
         << " \"" << devices[index].name << "\"");

    auto ptr = juce::MidiOutput::openDevice (devices[index].identifier);
    if (ptr == nullptr)
    {
        DBG ("[BluePrinter] MIDI: openDevice failed for \""
             << devices[index].name << "\", trying default");

        // Try the default device as a fallback
        auto def = juce::MidiOutput::getDefaultDevice();
        if (def.name.isNotEmpty())
        {
            DBG ("[BluePrinter] MIDI: default device \"" << def.name << "\"");
            ptr = juce::MidiOutput::openDevice (def.identifier);
        }
    }

    if (ptr != nullptr)
    {
        DBG ("[BluePrinter] MIDI: opened \"" << devices[index].name << "\"");
        midiOutput = std::move (ptr);
        midiOutputDeviceName = devices[index].name;

        // Stop any running clocks on connected gear so that the
        // next Start / clock train is clean.
        midiOutput->sendMessageNow (juce::MidiMessage::midiStop());
    }
    else
    {
        DBG ("[BluePrinter] MIDI: could not open any output device");
    }
}

void BluePrinterAudioProcessor::closeMidiOutputDevice()
{
    const juce::ScopedLock sl (midiOutputLock);
    if (midiOutput != nullptr)
    {
        DBG ("[BluePrinter] MIDI: closing device \"" << midiOutputDeviceName << "\"");
        midiOutput->sendMessageNow (juce::MidiMessage::midiStop());
        midiOutput.reset();
    }
}

juce::StringArray BluePrinterAudioProcessor::getAvailableMidiOutputDevices() const
{
    juce::StringArray names;
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        names.add (d.name);
    return names;
}

juce::String BluePrinterAudioProcessor::getLastSaveError() const
{
    juce::ScopedLock lock (libraryFolderLock);
    return lastSaveError;
}

std::map<juce::String, juce::String> BluePrinterAudioProcessor::getTagNames() const
{
    return tagNames;
}

void BluePrinterAudioProcessor::setTagName (const juce::String& colorKey, const juce::String& name)
{
    // Message thread only — this map feeds the UI snapshot directly.
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        tagNames.erase (colorKey);
    else
        tagNames[colorKey] = trimmed;

    // Debounce the disk write to the 30 Hz timerCallback (see
    // flushTagNamePersist): a synchronous write of the whole properties
    // file inside the WebView2 event dispatch blocks the browser's
    // message pump and can trip reentrancy crashes. The in-memory state
    // is already correct, so the UI snapshot stays fresh.
    tagPersistPending = true;
    tagPersistDeadline = juce::Time::currentTimeMillis() + 500;

    listeners.call ([](Listener& l) { l.libraryChanged(); });
}

void BluePrinterAudioProcessor::flushTagNamePersist()
{
    if (! tagPersistPending)
        return;

    tagPersistPending = false;
    if (auto* props = getUserState())
    {
        // Serialize as a flat JSON object {"red": "name", ...} without
        // any var/DynamicObject wrapping — plain string building, so
        // nothing reference-counted can dangle.
        juce::String json = "{";
        bool first = true;
        for (const auto& entry : tagNames)
        {
            if (! first)
                json += ",";
            first = false;
            json += juce::JSON::escapeString (entry.first);
            json += ":";
            json += juce::JSON::escapeString (entry.second);
        }
        json += "}";

        props->setValue ("tagNames", json);
        props->saveIfNeeded();
    }
}

int BluePrinterAudioProcessor::getSavedEditorWidth()
{
    if (auto* props = getUserState())
        return props->getIntValue ("editorWidth", 0);
    return 0;
}

int BluePrinterAudioProcessor::getSavedEditorHeight()
{
    if (auto* props = getUserState())
        return props->getIntValue ("editorHeight", 0);
    return 0;
}

void BluePrinterAudioProcessor::saveEditorSize (int width, int height)
{
    if (auto* props = getUserState())
    {
        props->setValue ("editorWidth", width);
        props->setValue ("editorHeight", height);
        props->saveIfNeeded();
    }
}

juce::String BluePrinterAudioProcessor::getLastChainRestoreError() const
{
    return lastChainRestoreError;
}

// Build the combined plugin-chain bundle that gets written to host
// state and the standalone properties file. Holds every chain's slots
// and routing config, plus the folder-wide blocklist and cached scan
// result on the shared library, and the chain-id counter so ids stay
// stable across restores.
//
// Format:
//   {
//     "chains": [
//       { "id": "chain0", "name": "...", "inputs": [0, 1],
//         "recordOnCapture": true, "wantsMidi": true, "slots": [...] },
//       ...
//     ],
//     "nextChainId":       5,
//     "blocklist":         ["...\\Foo.vst3", ...],
//     "availablePlugins":  [{ "name": ..., "path": ..., ... }, ...]
//   }
juce::var BluePrinterAudioProcessor::makeChainState() const
{
    auto* obj = new juce::DynamicObject();

    juce::Array<juce::var> chainsArray;
    {
        const juce::ScopedLock sl (chainLock);
        for (const auto& chain : chains)
            chainsArray.add (chain->getChainState());
    }
    obj->setProperty ("chains", chainsArray);
    obj->setProperty ("nextChainId", nextChainId);

    {
        juce::Array<juce::var> blocklistArray;
        for (const auto& path : vst3Library.getBlocklist())
            blocklistArray.add (path);
        obj->setProperty ("blocklist", blocklistArray);
    }

    const auto available = vst3Library.getAvailablePlugins();
    if (! available.isVoid())
        obj->setProperty ("availablePlugins", available);

    return juce::var (obj);
}

// Inverse of makeChainState. Accepts three formats:
//
//   1. The current chains-array format ("chains": [...]) — restored
//      verbatim, with ids validated by ensureUniqueChainIds.
//   2. The midiChain/audioChain-keyed split format — migrated to two
//      chains preserving each chain's slots and MIDI toggle.
//   3. The pre-split format with a single top-level "slots" array —
//      migrated to one chain holding the old guitar FX.
//
// Returns a (possibly empty) human-readable error string listing any
// plugins that were skipped; the caller surfaces it to the UI.

namespace
{
    // The crash handler (bluePrinterCrashHandler) writes
    // %APPDATA%/Retrokielto/crash-info.txt — keep the path in sync with
    // it. Returns the trailing plugin detail of the "Operation:" line,
    // e.g. "Operation: preparing plugin (finalizeAsyncLoad) Archetype
    // Tim Henson X.vst3" → "Archetype Tim Henson X.vst3". Only trusts
    // the file when it is fresher than propsMtimeBefore (the properties
    // file's state before this session wrote anything): the app is the
    // only writer of both files, so a newer crash-info was written by
    // the launch that just crashed, while an older one is stale
    // diagnostics from some previous incident and must not drive the
    // quarantine. Returns an empty string when the file is missing,
    // stale, or its op line names no plugin.
    juce::String readFreshCrashOpDetail (const juce::Time& propsMtimeBefore)
    {
        const auto crashFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                   .getChildFile ("Retrokielto")
                                   .getChildFile ("crash-info.txt");
        if (! crashFile.existsAsFile())
            return {};
        if (crashFile.getLastModificationTime() <= propsMtimeBefore)
            return {};

        const auto text = crashFile.loadFileAsString();
        for (const auto& line : juce::StringArray::fromLines (text))
        {
            if (! line.trim().startsWith ("Operation: "))
                continue;
            const auto op = line.trim().substring (juce::String ("Operation: ").length()).trim();
            // The op reads "<human step> (<api>) <plugin detail>"; split
            // at the last ") " so plugin names containing parens survive.
            const int split = op.lastIndexOf (") ");
            return split >= 0 ? op.substring (split + 2).trim() : juce::String();
        }
        return {};
    }

    // The lastPluginLoadOp property ("<epoch millis>:<file name>") is
    // written by notifyPluginLoadStarting before every plugin load and
    // cleared on load completion / restore drain. Unlike crash-info.txt
    // it also records fail-fast crashes (0xC0000409) that bypass the
    // unhandled-exception filter entirely and never touch crash-info.
    // Trusted under the same anchor rule as readFreshCrashOpDetail:
    // only when newer than the last clean exit (or, failing that, the
    // properties mtime at launch), so an op left over from a healthy
    // previous session can't quarantine an innocent plugin.
    juce::String readFreshLoadOpDetail (juce::PropertiesFile* props, const juce::Time& anchor)
    {
        if (props == nullptr)
            return {};

        const auto raw = props->getValue ("lastPluginLoadOp");
        if (raw.isEmpty())
            return {};

        const int colon = raw.indexOfChar (':');
        if (colon <= 0 || colon == raw.length() - 1)
            return {};

        const auto name = raw.substring (colon + 1).trim();
        if (name.isEmpty())
            return {};

        const auto writtenAt = juce::Time (raw.substring (0, colon).getLargeIntValue());
        if (writtenAt.toMilliseconds() <= 0 || writtenAt <= anchor)
            return {};

        return name;
    }
}

void BluePrinterAudioProcessor::applyChainState (const juce::var& state, juce::String& outError)
{
    auto* obj = state.getDynamicObject();
    if (obj == nullptr)
        return;

    // Self-healing restore. If the previous launch crashed mid-restore
    // (see the chainRestoreCrashed marker below), every plugin loads
    // with its defaults and the saved state blobs are skipped — a state
    // blob that crashes a plugin can never brick the app. The marker is
    // read before the restore starts and cleared only after the whole
    // restore has completed, so a crash at any point leaves it set.
    bool restoreStateBlobs = true;
    if (auto* props = getUserState())
    {
        // The chainRestoreCrashed marker is written at EVERY restore
        // start and cleared only when the deferred restore drains, so
        // it cannot distinguish a crashed session from a plain quit
        // mid-restore — quitting before the slow (heavy amp-sim)
        // restore finished leaves it set, and trusting it next launch
        // would skip every state blob and load all plugins with
        // DEFAULTS (the "plugin states lost on close" wipe). The
        // settings file is only ever written by a clean exit (the
        // standalone's closeButtonPressed -> savePluginState), so a
        // marker OLDER than its mtime is stale: the session that set it
        // ended cleanly and the on-disk blobs were never corrupted
        // (persistence is suppressed for the whole restore). DAW hosts
        // have no settings file; there the marker is honored as before.
        // Decided once per process: the standalone can run
        // applyChainState twice in one launch (settings-file restore in
        // setStateInformation, then the editor's
        // restoreSavedPluginChains), and the second call must not
        // re-evaluate the marker the first call just wrote.
        if (! chainRestoreDecisionMade)
        {
            chainRestoreDecisionMade = true;
            const bool markerWasSet = props->getBoolValue ("chainRestoreCrashed", false);
            // Stored as a string: epoch millis exceed PropertiesFile's
            // 32-bit getIntValue.
            const auto markerSetAt = juce::Time (props->getValue ("chainRestoreMarkerTime").getLargeIntValue());
            chainRestoreBlobsAllowed = ! markerWasSet;
            if (markerWasSet && markerSetAt.toMilliseconds() > 0)
            {
                const auto settingsFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                              .getChildFile ("BluePrinter")
                                              .getChildFile ("BluePrinter.settings");
                if (settingsFile.existsAsFile()
                    && settingsFile.getLastModificationTime() > markerSetAt)
                    chainRestoreBlobsAllowed = true; // clean exit since the marker was written
            }
        }
        restoreStateBlobs = chainRestoreBlobsAllowed;
        // Fallback freshness anchor: the file's mtime right now, before
        // this session's marker write. (restoreUserState captured a
        // better one — userStateMtimeAtLaunch — before ANY write this
        // session; on startup persistLibraryFolder has usually already
        // bumped the file by the time we get here.)
        const auto propsMtimeBefore = props->getFile().getLastModificationTime();
        props->setValue ("chainRestoreCrashed", true);
        props->setValue ("chainRestoreMarkerTime",
                         juce::String (juce::Time::currentTimeMillis()));
        props->saveIfNeeded();

        // ---- Crash diagnostics (read on EVERY launch, not just when
        // the marker was set). A crash that happens outside a deferred
        // restore — a manual plugin add, or a plugin dying after the
        // restore drained — leaves the marker clear, and those crashes
        // need the quarantine to engage just as much as mid-restore
        // ones do (the Archetype "X" heap faults happen in manual adds
        // too). Trust anchor: the LAST CLEAN EXIT (BluePrinter.settings
        // mtime) rather than the properties mtime — a crashed session
        // writes the marker into the properties AFTER its own crash
        // diagnostics landed, so comparing against the properties made
        // the very crash we just suffered look stale. The settings file
        // is only ever written by a clean exit, so any crash-info newer
        // than it belongs to the session that just died. In DAW hosts
        // the settings file doesn't exist; fall back to the launch-time
        // properties mtime.
        const auto settingsFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                      .getChildFile ("BluePrinter")
                                      .getChildFile ("BluePrinter.settings");
        const auto anchor = settingsFile.existsAsFile()
                                ? settingsFile.getLastModificationTime()
                                : (userStateMtimeAtLaunch != juce::Time()
                                       ? userStateMtimeAtLaunch
                                       : propsMtimeBefore);

        // Parse the crash diagnostics now; the restore driver below
        // quarantines whatever they name instead of instantiating it
        // again.
        crashedPluginDetail = readFreshCrashOpDetail (anchor);
        // Fail-fast crashes (0xC0000409) never reach the exception
        // filter, so crash-info.txt can stay frozen at an older crash
        // while a later, unrecorded one is the real killer. The
        // lastPluginLoadOp property (written on the message thread
        // before every plugin load) closes that gap.
        loadOpCrashDetail = readFreshLoadOpDetail (props, anchor);

        // Self-heal mode: either the blobs were skipped or a plugin was
        // quarantined, so the in-memory chains hold defaults. While
        // true, getStateInformation omits pluginChains (see there);
        // cleared on the first real user mutation (persistPluginChain).
        stateRestoreSkippedThisLaunch.store (! restoreStateBlobs
                                     || crashedPluginDetail.isNotEmpty()
                                     || loadOpCrashDetail.isNotEmpty(),
                                     std::memory_order_relaxed);
    }
    quarantinedSkippedThisRestore.clear();
    // Shared by all chains (setChainState reads it via the library), so
    // chains created later in this restore inherit the setting.
    vst3Library.setSkipStateRestore (! restoreStateBlobs);

    // Keep the deferred-restore driver (timerCallback) paused while a
    // restore is in progress: clearChains() below would destroy a chain
    // whose async load is still in flight. In the standalone both
    // restores complete before the message loop starts pumping, so the
    // driver only ever sees the settled state.
    restoreActive = true;

    // Suppress persistence for the whole restore. clearChains() and the
    // per-chain setChainState() fire onChanged, which would otherwise
    // echo the mid-restore (partial/empty) state back into the
    // properties file — the standalone wrapper calls setStateInformation
    // at startup (reloadPluginState), and without this guard that echo
    // wiped the saved chains on every launch. Nested suppression is
    // fine (restoreSavedPluginChains also sets the flag).
    const bool wasPersisting = persistingPluginChain.load (std::memory_order_acquire);
    persistingPluginChain.store (true, std::memory_order_release);

    // Blocklist first so each chain's setChainState can check it. The
    // blocklist is folder-wide, so restoring it is a single set
    // regardless of which chains the state contains.
    if (auto* blocklistVar = obj->getProperty ("blocklist").getArray())
    {
        juce::StringArray paths;
        for (const auto& v : *blocklistVar)
            paths.add (v.toString());
        vst3Library.setBlocklist (paths);
    }

    // Cached scan result. Old saved states won't have this; in that
    // case we leave availablePlugins untouched (it'll be an empty
    // var and the UI will show no available plugins until the user
    // re-scans).
    if (obj->hasProperty ("availablePlugins"))
        vst3Library.setAvailablePlugins (obj->getProperty ("availablePlugins"));

    clearChains();

    const bool splitFormat = obj->hasProperty ("midiChain")
                          || obj->hasProperty ("audioChain");
    const bool newFormat = obj->hasProperty ("chains");

    if (! newFormat)
    {
        // Legacy formats. Both chains default to the same behaviour the
        // old code had: MIDI chain sees the keyboard, audio FX chain
        // doesn't, both take the full stereo input and record.
        if (splitFormat)
        {
            juce::String midiError, audioError;

            auto* midiChain = createChain ("MIDI Chain", ChainInputBoth, true, true);
            const auto midiVar = obj->getProperty ("midiChain");
            if (midiVar.isObject())
            {
                midiChain->setChainState (midiVar, midiError);
                if (! midiVar.getDynamicObject()->hasProperty ("wantsMidi"))
                    midiChain->setWantsMidi (true);
            }

            auto* audioChain = createChain ("Audio FX Chain", ChainInputBoth, false, true);
            const auto audioVar = obj->getProperty ("audioChain");
            if (audioVar.isObject())
            {
                audioChain->setChainState (audioVar, audioError);
                if (! audioVar.getDynamicObject()->hasProperty ("wantsMidi"))
                    audioChain->setWantsMidi (false);
            }

            if (midiError.isNotEmpty())
            {
                if (outError.isNotEmpty()) outError += "\n";
                outError += "MIDI chain: " + midiError;
            }
            if (audioError.isNotEmpty())
            {
                if (outError.isNotEmpty()) outError += "\n";
                outError += "Audio chain: " + audioError;
            }
        }
        else
        {
            // Old { slots, blocklist, availablePlugins } shape.
            juce::String audioError;
            auto* chain = createChain ("Audio FX Chain", ChainInputBoth, false, true);
            chain->setChainState (obj->getProperty ("slots"), audioError);
            if (audioError.isNotEmpty())
            {
                if (outError.isNotEmpty()) outError += "\n";
                outError += "Audio chain: " + audioError;
            }
        }
    }
    else
    {
        // Current format. Each chain object carries its own id/name/
        // routing config; setChainState restores those plus the slots.
        auto chainArray = obj->getProperty ("chains");
        if (auto* arr = chainArray.getArray())
        {
            for (const auto& chainVar : *arr)
            {
                if (! chainVar.isObject())
                    continue;
                auto* chain = createChain ({}, ChainInputBoth, true, true);
                juce::String chainError;
                chain->setChainState (chainVar, chainError);
                if (chainError.isNotEmpty())
                {
                    if (outError.isNotEmpty()) outError += "\n";
                    outError += "Chain: " + chainError;
                }
            }
        }
    }

    // The saved state may predate the id counter or contain duplicate
    // ids (hand-edited files); fix both so every chain has a stable,
    // unique id.
    if (obj->hasProperty ("nextChainId"))
        nextChainId = juce::jmax (nextChainId, static_cast<int> (obj->getProperty ("nextChainId")));
    ensureUniqueChainIds();

    restoreActive = false;

    persistingPluginChain.store (wasPersisting, std::memory_order_release);

    // Crash-marker lifecycle. The marker was set above and must stay
    // set until the WHOLE restore has completed. When saved slots load
    // deferred — one per timer tick, AFTER this function returns (the
    // normal case) — a crash in one of those loads (e.g. an Archetype
    // "X" amp sim dying in setStateInformation) leaves the marker set
    // so the next launch skips the state blobs. Clearing it here while
    // slots are still queued made the same crashing blob apply on
    // every launch: crash, clear-marker, crash again. Only clear
    // immediately when nothing is deferred; otherwise the timer driver
    // clears it once the deferred restore drains.
    if (! isChainRestoreInProgress())
    {
        restoreRequestedThisSession = false;
        // The crash op names the last restore step; once the restore has
        // drained it is stale — a crash later in the session (device
        // start, plugin audio) would otherwise be misattributed to a
        // restore slot and quarantine an innocent plugin on the next
        // launch. Reset it with the restore.
        BluePrinterAudioProcessor::setCrashOp ("no chain operation in progress");
        // Same for the persisted load-op: nothing is loading anymore, so
        // a crash later in the session must not quarantine the last
        // successfully loaded slot.
        notifyPluginLoadFinished();
        if (auto* props = getUserState())
        {
            props->setValue ("chainRestoreCrashed", false);
            props->saveIfNeeded();
        }
    }
    else
    {
        restoreRequestedThisSession = true;
    }
}

void BluePrinterAudioProcessor::clearLastChainRestoreError()
{
    lastChainRestoreError.clear();
}

//==============================================================================
// Plugin quarantine (see isPluginQuarantined in the header).

void BluePrinterAudioProcessor::loadPluginQuarantine()
{
    if (pluginQuarantineLoaded)
        return;
    pluginQuarantineLoaded = true;

    auto* props = getUserState();
    if (props == nullptr)
        return;
    const auto arr = juce::JSON::parse (props->getValue ("pluginQuarantine"));
    if (const auto* a = arr.getArray())
        for (const auto& v : *a)
            if (v.toString().trim().isNotEmpty())
                pluginQuarantine.addIfNotAlreadyThere (v.toString().trim());
}

void BluePrinterAudioProcessor::savePluginQuarantine()
{
    auto* props = getUserState();
    if (props == nullptr)
        return;
    juce::Array<juce::var> arr;
    for (const auto& n : pluginQuarantine)
        arr.add (juce::var (n));
    props->setValue ("pluginQuarantine", juce::JSON::toString (juce::var (arr)));
    props->saveIfNeeded();
}

bool BluePrinterAudioProcessor::isPluginQuarantined (const juce::String& fileName)
{
    loadPluginQuarantine();
    return pluginQuarantine.contains (fileName);
}

void BluePrinterAudioProcessor::clearPluginQuarantineForFile (const juce::String& fileName)
{
    loadPluginQuarantine();
    if (! pluginQuarantine.contains (fileName))
        return;
    while (pluginQuarantine.contains (fileName))
        pluginQuarantine.removeString (fileName);
    savePluginQuarantine();
}

// Persist "lastPluginLoadOp" ("<epoch millis>:<plugin file name>")
// before a plugin load starts. A crash during the load — including the
// fail-fast class (STATUS_STACK_BUFFER_OVERRUN: the Neural DSP "X"
// stack-cookie deaths) that never reaches SetUnhandledExceptionFilter
// and so never updates crash-info.txt — leaves this as the only
// nameable suspect on disk, and the next launch's quarantine
// (readFreshLoadOpDetail) consumes it. Message thread only; the load
// drivers all run there.
void BluePrinterAudioProcessor::notifyPluginLoadStarting (const juce::String& fileName)
{
    if (auto* props = getUserState())
    {
        props->setValue ("lastPluginLoadOp",
                         juce::String (juce::Time::currentTimeMillis()) + ":" + fileName);
        props->saveIfNeeded();
    }
}

void BluePrinterAudioProcessor::notifyPluginLoadFinished()
{
    // Empty detail (just the timestamp) marks "no load in flight".
    if (auto* props = getUserState())
    {
        props->setValue ("lastPluginLoadOp",
                         juce::String (juce::Time::currentTimeMillis()) + ":");
        props->saveIfNeeded();
    }
}

// Fold a skipped (quarantined) plugin into the restore error the UI
// shows, with the remedy spelled out.
void BluePrinterAudioProcessor::recordQuarantinedSkip (const juce::String& fileName, const juce::String& pluginName)
{
    const auto shown = pluginName.isNotEmpty() ? pluginName : fileName;
    if (! quarantinedSkippedThisRestore.contains (shown))
        quarantinedSkippedThisRestore.add (shown);

    lastChainRestoreError = "Skipped "
        + quarantinedSkippedThisRestore.joinIntoString (", ")
        + " — it crashed BluePrinter on a previous launch. Re-add it from a chain's plugin list to try again.";
}

//==============================================================================
void BluePrinterAudioProcessor::timerCallback()
{
    // Flush the debounced chain save once it has been quiet for 500 ms.
    if (chainPersistPending.load (std::memory_order_acquire)
        && juce::Time::currentTimeMillis() >= chainPersistDeadline.load (std::memory_order_acquire))
        flushPendingChainPersist();

    // Flush the debounced tag-name save the same way — never inside the
    // WebView2 event dispatch that armed it.
    if (tagPersistPending && juce::Time::currentTimeMillis() >= tagPersistDeadline)
        flushTagNamePersist();

    // Flush the debounced snippet-gain save (Gain knob drags).
    if (snippetGainPersistPending
        && juce::Time::currentTimeMillis() >= snippetGainPersistDeadline)
        flushSnippetGainPersist();

    // Deferred chain restore. Saved plugin slots are queued as pending
    // slots by setChainState and loaded here, ONE per timer tick (i.e.
    // one per message-loop turn). Loading them synchronously inside the
    // restore kept the message thread inside plugin code for hundreds
    // of ms; a window message the plugins queue during their own setup
    // (their windows are created at instantiation) then got dispatched
    // reentrantly and crashed some plugins — Neural DSP "X" amp sims
    // died with a heap fault in the first instance's window proc. One
    // slot per loop turn gives every plugin an idle gap for its pending
    // messages to fire safely before the next plugin is created.
    if (pendingPluginLoads.load (std::memory_order_acquire) == 0 && ! restoreActive)
    {
        // Kick the NEXT pending slot's load off first. addPluginAsync
        // posts the createInstance to the plugin UI apartment and
        // returns immediately, so its heavy instantiation overlaps with
        // the previous slot's setStateInformation below on this (main)
        // thread — two distinct plugin instances on two distinct
        // threads — instead of running strictly back-to-back. This hides
        // every saved-state apply behind the next plugin's load, leaving
        // only the LAST slot's state poke exposed as the restore tail.
        // The state poke itself is applied one message-loop turn after
        // its plugin finished loading (see below).
        PluginChain* target = nullptr;
        for (auto& chain : chains)
        {
            if (chain->hasPendingSlots())
            {
                target = chain.get();
                break;
            }
        }

        if (target != nullptr)
        {
            auto slot = target->popPendingSlot();
            // Copy the file BEFORE any lambda capture moves the slot
            // (see the MSVC evaluation-order note below).
            const auto slotFile = slot.file;

            // Quarantine: a plugin that crashed the app on a previous
            // launch is never instantiated again until the user re-adds
            // it manually (addVst3FromPath clears the entry first). The
            // chainRestoreCrashed marker alone can't defuse this crash
            // class — it skips state blobs, but the Neural DSP "X" heap
            // faults happen in createPluginInstance/prepareToPlay,
            // before any state is applied. On a crash launch the
            // plugins named in crash-info.txt AND in the persisted
            // lastPluginLoadOp (covers fail-fast crashes that never
            // reach the exception filter) join the quarantine here, so
            // the very next pop can't re-run them either.
            const auto slotFileName = slotFile.getFileName();
            const auto matchDetail = [&slotFileName, &slot] (const juce::String& detail)
            {
                return detail.isNotEmpty()
                    && (detail.equalsIgnoreCase (slotFileName)
                        || (slot.name.isNotEmpty() && detail.equalsIgnoreCase (slot.name)));
            };
            const bool matchesLastCrash = matchDetail (crashedPluginDetail)
                                       || matchDetail (loadOpCrashDetail);
            if (isPluginQuarantined (slotFileName) || matchesLastCrash)
            {
                if (matchesLastCrash && ! isPluginQuarantined (slotFileName))
                {
                    pluginQuarantine.addIfNotAlreadyThere (slotFileName);
                    savePluginQuarantine();
                }
                recordQuarantinedSkip (slotFileName, slot.name);
                listeners.call ([this] (Listener& l) { l.pluginChainChanged(); });
                // Slot intentionally dropped — the next tick pops the
                // next pending one and the restore drains without it.
            }
            // exists() (not existsAsFile()): a saved slot may reference
            // a .vst3 bundle directory (its binary lives under
            // Contents/x86_64-win/) rather than a loose binary file.
            else if (slotFile.exists())
            {
                pendingPluginLoads.store (1, std::memory_order_release);
                const auto chainId = target->getChainId();
                // Record the plugin about to load in the properties file
                // so even an unrecorded fail-fast mid-load leaves a
                // quarantinable suspect for the next launch.
                notifyPluginLoadStarting (slotFileName);
                // 30 s per slot: heavy amp sims (Neural DSP "X" etc.)
                // can take a long time to instantiate on a cold start,
                // and a timeout here silently drops the plugin.
                target->addPluginAsync (slotFile, 30000,
                    [this, chainId, slot = std::move (slot)] (int slotIndex,
                                                              const juce::String&,
                                                              const juce::String&,
                                                              bool) mutable
                    {
                        pendingPluginLoads.store (0, std::memory_order_release);
                        // The load really finished (timeouts leave the
                        // worker running and are NOT cleared).
                        if (slotIndex >= 0)
                            notifyPluginLoadFinished();
                        auto* chain = getChainById (chainId);
                        if (chain == nullptr || slotIndex < 0)
                            return;

                        chain->setBypass (slotIndex, slot.bypassed);

                        // Queue the saved state blob for the NEXT
                        // message-loop turn instead of applying it
                        // here. This callback runs on the message
                        // thread right after the plugin's window was
                        // created (on the load worker), with its queued
                        // window messages still undispatched; poking
                        // setStateInformation then made some plugins
                        // run their window proc reentrantly and die
                        // with a heap fault (Neural DSP "X" amp sims).
                        // One idle loop turn puts the pump between the
                        // window's creation and the state poke.
                        // Self-healing mode (a previous launch crashed
                        // mid-restore) skips the saved blobs. Bypass is
                        // BluePrinter's own per-slot audio-routing flag,
                        // not the plugin's state — a slot saved bypassed
                        // still holds a state worth restoring, and
                        // skipping it made every bypassed slot come back
                        // in DEFAULTS every launch (the X amp sims'
                        // "state lost on close" wipe).
                        if (slot.stateBase64.isNotEmpty()
                            && ! vst3Library.getSkipStateRestore())
                        {
                            juce::MemoryBlock stateData;
                            if (stateData.fromBase64Encoding (slot.stateBase64))
                                pendingStateApply = std::make_unique<PendingStateApply> (
                                    PendingStateApply { chainId, slotIndex, std::move (stateData) });
                        }

                        listeners.call ([](Listener& l) { l.pluginChainChanged(); });
                    });
            }
        }

        // Apply the previous slot's saved state now — one message-loop
        // turn after its plugin finished loading (the load callback
        // queued it into pendingStateApply), never inside the load
        // callback. The gap lets the plugin's queued window messages be
        // dispatched by the normal pump first: dispatching them
        // reentrantly from inside setStateInformation killed some
        // plugins (Neural DSP "X" amp sims — heap fault in the first
        // instance's window proc). It runs on the main thread, NOT the
        // plugin's apartment, so the apartment keeps pumping the
        // plugin's windows during the poke while this thread blocks.
        // With the next slot's load already started above, the poke
        // overlaps that load on the apartment thread instead of
        // stalling the whole restore.
        if (pendingStateApply != nullptr)
        {
            auto apply = std::move (pendingStateApply);
            if (auto* chain = getChainById (apply->chainId))
            {
                if (auto* plugin = chain->getPlugin (apply->slotIndex))
                {
                    BluePrinterAudioProcessor::setCrashOp ("restoring plugin state (setStateInformation)",
                                                           plugin->getName().toRawUTF8());
                    plugin->setStateInformation (apply->state.getData(),
                                                 static_cast<int> (apply->state.getSize()));
                }
            }
        }
    }

    // Crash-marker lifecycle (see applyChainState): the marker stays
    // set while the deferred restore has anything queued and is
    // cleared here once it has fully drained — a crash at any point
    // (instantiation or state apply) leaves it set, so the next launch
    // self-heals by loading plugins with defaults.
    if (restoreRequestedThisSession && ! isChainRestoreInProgress())
    {
        restoreRequestedThisSession = false;
        // Same stale-op reset as above (this is the no-deferred-loads
        // path inside applyChainState itself).
        BluePrinterAudioProcessor::setCrashOp ("no chain operation in progress");
        notifyPluginLoadFinished();
        if (auto* props = getUserState())
        {
            props->setValue ("chainRestoreCrashed", false);
            props->saveIfNeeded();
        }

        // The deferred restore has fully drained: push a fresh chain
        // snapshot so the frontend learns restoring == false and the
        // splash can leave the "Applying saved state" tail. Nothing else
        // fires pluginChainChanged after the last slot's state is
        // applied, so without this the UI is left holding a stale
        // restoring == true snapshot and the splash lingers even though
        // the restore is actually done.
        listeners.call ([](Listener& l) { l.pluginChainChanged(); });
    }

    if (recordingFinalizePending.exchange (false, std::memory_order_acq_rel))
        finalizeRecordingOnMessageThread();

    // Peak meter decay.
    const float prevPeak = inputPeak.load (std::memory_order_acquire);
    if (prevPeak > 0.001f)
        inputPeak.store (prevPeak * 0.92f, std::memory_order_release);

    const float prevRecordPeak = recordPeak.load (std::memory_order_acquire);
    if (prevRecordPeak > 0.001f)
        recordPeak.store (prevRecordPeak * 0.92f, std::memory_order_release);

    const float prevOutputPeak = outputPeak.load (std::memory_order_acquire);
    if (prevOutputPeak > 0.001f)
        outputPeak.store (prevOutputPeak * 0.92f, std::memory_order_release);

    const float prevLoopPeak = loopPlayPeak.load (std::memory_order_acquire);
    if (prevLoopPeak > 0.001f)
        loopPlayPeak.store (prevLoopPeak * 0.92f, std::memory_order_release);

    // The loop level is only written by the audio thread while the loop
    // plays, so let it fall to zero here once playback stops.
    if (! audioLoopPlaying.load (std::memory_order_acquire))
    {
        const float prevLoopLevel = loopPlayLevel.load (std::memory_order_acquire);
        if (prevLoopLevel > 0.001f)
            loopPlayLevel.store (prevLoopLevel * 0.85f, std::memory_order_release);
    }

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::finalizeRecordingOnMessageThread()
{
    // The take ended (possibly by filling the max-length buffer on the
    // audio thread): release the clock if no other source wants it.
    updateClockRunState();

    if (recordBuffer == nullptr)
        return;

    // The take is not saved automatically anymore — it becomes a pending
    // take the user reviews, replays, then explicitly saves to the
    // library or discards. The audio stays in recordBuffer (the shared
    // capture buffer) until a new capture invalidates it.
    int captured = 0;
    {
        const juce::ScopedLock sl (recordLock);
        captured = static_cast<int> (recordWritePos.load (std::memory_order_acquire));
        recordWritePos.store (0, std::memory_order_release);
        if (captured <= 0)
            return;
    }

    takePlaybackActive.store (false, std::memory_order_release);
    takePlaybackPos.store (0, std::memory_order_release);
    takeLength.store (captured, std::memory_order_release);
    takePending.store (true, std::memory_order_release);
    refreshTakePeaks();

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::refreshTakePeaks()
{
    takePeaks.clear();

    const auto length = takeLength.load (std::memory_order_acquire);
    if (recordBuffer == nullptr || length <= 0)
        return;

    // Copy the take region out under the lock (guards against a new
    // capture writing concurrently) and downsample for the UI.
    juce::AudioBuffer<float> region (recordBuffer->getNumChannels(), static_cast<int> (length));
    {
        const juce::ScopedLock sl (recordLock);
        for (int ch = 0; ch < region.getNumChannels(); ++ch)
            region.copyFrom (ch, 0, *recordBuffer, ch, 0, static_cast<int> (length));
    }
    takePeaks = SnippetLibrary::computePeaks (region, 256);
}

void BluePrinterAudioProcessor::clearPendingTake()
{
    takePending.store (false, std::memory_order_release);
    takePlaybackActive.store (false, std::memory_order_release);
    takePlaybackPos.store (0, std::memory_order_release);
    takeLength.store (0, std::memory_order_release);
    takePeaks.clear();
}

void BluePrinterAudioProcessor::setTakePlayback (bool enabled)
{
    if (enabled)
    {
        if (! takePending.load (std::memory_order_acquire)
            || takeLength.load (std::memory_order_acquire) <= 0)
            return;

        // One reviewer at a time: stop snippet playback and the looper
        // so the take review is the only thing playing.
        stopPlayback();
        if (audioLoopPlaying.load (std::memory_order_acquire))
            setLooperPlaying (false);

        takePlaybackPos.store (0, std::memory_order_release);
        takePlaybackActive.store (true, std::memory_order_release);
    }
    else
    {
        takePlaybackActive.store (false, std::memory_order_release);
        takePlaybackPos.store (0, std::memory_order_release);
    }

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::savePendingTake()
{
    if (! takePending.load (std::memory_order_acquire))
        return;

    const auto captured = takeLength.load (std::memory_order_acquire);
    if (recordBuffer == nullptr || captured <= 0)
    {
        clearPendingTake();
        listeners.call ([](Listener& l) { l.transportChanged(); });
        return;
    }

    takePlaybackActive.store (false, std::memory_order_release);
    takePlaybackPos.store (0, std::memory_order_release);

    std::shared_ptr<Snippet> snippet;

    {
        const juce::ScopedLock sl (recordLock);
        const int channels = recordBuffer->getNumChannels();
        auto snippetBuffer = std::make_shared<juce::AudioBuffer<float>> (channels, static_cast<int> (captured));
        for (int ch = 0; ch < channels; ++ch)
            snippetBuffer->copyFrom (ch, 0, *recordBuffer, ch, 0, static_cast<int> (captured));

        auto defaultName = "Snippet " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");
        snippet = library.addSnippet (snippetBuffer, getSampleRate(), defaultName);
    }

    clearPendingTake();
    listeners.call ([](Listener& l) { l.libraryChanged(); l.transportChanged(); });

    if (snippet != nullptr)
    {
        juce::File folder;
        {
            juce::ScopedLock lock (libraryFolderLock);
            folder = libraryFolder;
        }
        if (folder.isDirectory())
        {
            juce::String outPath, outError;
            if (library.saveSnippetToFolder (*snippet, folder, outPath, outError))
                library.markSaved (snippet->id, outPath);
            else
            {
                juce::ScopedLock lock (libraryFolderLock);
                lastSaveError = outError;
                listeners.call ([](Listener& l) { l.libraryChanged(); });
            }
        }
    }
}

void BluePrinterAudioProcessor::discardPendingTake()
{
    if (! takePending.load (std::memory_order_acquire))
        return;

    clearPendingTake();
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

//==============================================================================
juce::AudioProcessorEditor* BluePrinterAudioProcessor::createEditor()
{
    auto webViewOptions = juce::WebBrowserComponent::Options{}
                              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2);

    if (!juce::WebBrowserComponent::areOptionsSupported(webViewOptions))
        return new BluePrinterAudioProcessorEditor(*this);

    return new BluePrinterWebViewEditor(*this);
}

bool BluePrinterAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
void BluePrinterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // Stash the non-automatable metronome settings in the same ValueTree
    // so they survive a project save/load.
    state.setProperty ("metronomeEnabled", metronomeEnabled.load(), nullptr);
    state.setProperty ("bpm",              bpm.load(),              nullptr);
    state.setProperty ("countInBeats",     countInBeats.load(),     nullptr);
    state.setProperty ("loopLevel",        loopLevel.load(),        nullptr);
    state.setProperty ("dryLevel",         dryLevel.load(),         nullptr);
    state.setProperty ("overdubLevel",     overdubLevel.load(),     nullptr);
    state.setProperty ("midiClockEnabled", midiClockEnabled.load(), nullptr);
    state.setProperty ("midiClockOnRecord", midiClockOnRecord.load(), nullptr);
    state.setProperty ("clickDuringCapture", clickDuringCapture.load(), nullptr);
    state.setProperty ("midiDeviceName",   midiOutputDeviceName,    nullptr);
    // Click sound tuning.
    state.setProperty ("clickPitch",        clickPitch,        nullptr);
    state.setProperty ("clickAccentPitch",  clickAccentPitch,  nullptr);
    state.setProperty ("clickDecay",        clickDecay,        nullptr);
    state.setProperty ("clickVolume",       clickVolume,       nullptr);
    state.setProperty ("clickAccentVolume", clickAccentVolume, nullptr);
    state.setProperty ("clickNoise",        clickNoise,        nullptr);
    // VST3 chains: per-slot path + bypass + base64 plugin state for
    // both the MIDI and the audio chain, plus the folder-wide
    // blocklist and cached scan result. Stored as a JSON string so
    // ValueTree can carry an arbitrary blob.
    // SKIPPED while the deferred restore is still loading: the
    // standalone writes this blob to its settings file on every exit,
    // so quitting mid-restore would save the partial (empty) chain
    // bundle, and the next launch would restore that instead of the
    // last good state. Omitting the property makes the next launch
    // fall back to the properties file, which still holds the good
    // chains. The same applies for the whole self-heal launch
    // (stateRestoreSkippedThisLaunch): blobs were skipped and/or a
    // crashed plugin quarantined, so the in-memory chains hold DEFAULT
    // plugin states — capturing them at exit would overwrite the good
    // on-disk states (the "plugin state lost on close" wipe). The first
    // real user mutation (persistPluginChain) clears the flag, at which
    // point the defaults are user-accepted and capture resumes.
    if (! isChainRestoreInProgress()
        && ! stateRestoreSkippedThisLaunch.load (std::memory_order_relaxed))
        state.setProperty ("pluginChains", juce::JSON::toString (makeChainState(), true), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void BluePrinterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xmlState);
            apvts.replaceState (state);

            metronomeEnabled.store (static_cast<bool>  (state.getProperty ("metronomeEnabled", true)));
            bpm.store              (static_cast<float> (state.getProperty ("bpm",              120.0f)));
            countInBeats.store     (static_cast<int>   (state.getProperty ("countInBeats",     4)));
            loopLevel.store        (static_cast<float> (state.getProperty ("loopLevel",        0.0f)));
            dryLevel.store         (static_cast<float> (state.getProperty ("dryLevel",         0.0f)));
            overdubLevel.store     (static_cast<float> (state.getProperty ("overdubLevel",     0.0f)));
            midiClockEnabled.store (static_cast<bool>  (state.getProperty ("midiClockEnabled", false)));
            midiClockOnRecord.store (static_cast<bool> (state.getProperty ("midiClockOnRecord", false)));
            // Header-level click-during-capture gate. Old builds had
            // separate take/looper toggles; migrate by keeping the old
            // behaviour (on unless BOTH old toggles were off).
            clickDuringCapture.store (static_cast<bool> (state.getProperty (
                "clickDuringCapture",
                static_cast<bool> (state.getProperty ("clickDuringTake", true))
                    || static_cast<bool> (state.getProperty ("looperClickDuringCapture", true)))));
            midiOutputDeviceName  = state.getProperty ("midiDeviceName", juce::String()).toString();

            // Click sound tuning (defaults match resynthesizeClicks).
            clickPitch        = static_cast<float> (state.getProperty ("clickPitch",        1000.0f));
            clickAccentPitch  = static_cast<float> (state.getProperty ("clickAccentPitch",  1500.0f));
            clickDecay        = static_cast<float> (state.getProperty ("clickDecay",         90.0f));
            clickVolume       = static_cast<float> (state.getProperty ("clickVolume",         0.35f));
            clickAccentVolume = static_cast<float> (state.getProperty ("clickAccentVolume",   0.50f));
            clickNoise        = static_cast<float> (state.getProperty ("clickNoise",          0.10f));
            resynthesizeClicks();

            // Read either the new "pluginChains" key or the pre-split
            // "pluginChain" key. The old key is the single-chain
            // format that applyChainState maps onto the audio chain.
            const auto chainJson = state.getProperty ("pluginChains").toString().isNotEmpty()
                ? state.getProperty ("pluginChains").toString()
                : state.getProperty ("pluginChain").toString();
            if (chainJson.isNotEmpty())
            {
                const auto chainVar = juce::JSON::parse (chainJson);
                juce::String error;
                applyChainState (chainVar, error);
                if (error.isNotEmpty())
                {
                    // Stash for the UI to display when it opens.
                    lastChainRestoreError = error;
                }
        }

        // If MIDI clock was enabled in a previous session, re-open the
        // output device so external gear picks up right away. Skipped in
        // "on record" mode: there the clock starts when a capture starts
        // (updateClockRunState opens the device then), not at launch.
        // This is deferred to the message thread because
        // MidiOutput::openDevice must run there.
        if (midiClockEnabled.load (std::memory_order_acquire)
            && ! midiClockOnRecord.load (std::memory_order_acquire))
        {
            juce::MessageManager::callAsync ([this]
            {
                const juce::ScopedLock sl (midiOutputLock);
                if (midiOutput == nullptr)
                    openMidiOutputDevice();
            });
        }
    }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout BluePrinterAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Input trim (record level) in dB.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "Gain",
        "Input",
        juce::NormalisableRange<float> (-12.0f, 24.0f, 0.5f),
        0.0f));

    // Master monitor Output in dB. The ID is kept as "PlaybackVolume"
    // so saved sessions keep their value; it no longer scales only
    // playback — it is the final output gain.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "PlaybackVolume",
        "Output",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.5f),
        0.0f));

    return layout;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BluePrinterAudioProcessor();
}

//==============================================================================
// User-state persistence (standalone-only — DAW hosts persist their own
// state via getStateInformation/setStateInformation and we leave that
// path alone).
//
// File (resolved by PropertiesFile::Options::getDefaultFile):
//   Windows: %APPDATA%\Retrokielto\BluePrinter.properties
//   macOS:   ~/Library/Application Support/Retrokielto/BluePrinter.properties
//   Linux:   ~/.config/Retrokielto/BluePrinter.properties
//
// Failure mode: any disk error (read-only volume, missing perms) leaves
// userState == nullptr and every save/restore call becomes a no-op. The
// app still runs in-memory; only the remember-across-launches behaviour
// degrades.

juce::PropertiesFile* BluePrinterAudioProcessor::getUserState()
{
    if (userState == nullptr)
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = "BluePrinter";
        opts.filenameSuffix      = ".properties";
        opts.folderName          = "Retrokielto";
        // Required on Mac — without it JUCE 8 asserts. "Application
        // Support" is the Apple-recommended location.
        opts.osxLibrarySubFolder = "Application Support";
        opts.commonToAllUsers    = false;
        // XML is the only StorageFormat option in JUCE 8 (INI was
        // removed). The file is tiny so format doesn't matter.
        opts.storageFormat       = juce::PropertiesFile::storeAsXML;
        // Default is "save a few seconds after a change", which means a
        // crash mid-session can lose the last mutation. Force synchronous
        // writes so each setValue hits disk before we return.
        opts.millisecondsBeforeSaving = 0;
        userState = std::make_unique<juce::PropertiesFile> (opts);

        // The default file lives under the per-platform app-data dir;
        // PropertiesFile doesn't auto-create the parent directory, so
        // make sure it exists before the first save.
        if (auto* p = userState.get())
            p->getFile().getParentDirectory().createDirectory();
    }
    return userState.get();
}

juce::var BluePrinterAudioProcessor::loadSavedChainState()
{
    auto* props = getUserState();
    if (props == nullptr)
        return {};

    const auto newJson = props->getValue ("pluginChains");
    const auto oldJson = props->getValue ("pluginChain");

    auto parse = [](const juce::String& json) -> juce::var
    {
        return json.isNotEmpty() ? juce::JSON::parse (json) : juce::var();
    };
    const auto newVar = parse (newJson);
    const auto oldVar = parse (oldJson);

    // The current-format key is authoritative whenever it parses to a
    // valid bundle object at all: a pluginChains written by this build
    // (even one holding empty chains — the user removed the plugins)
    // always wins over the legacy pre-split key, which belongs to an
    // older save and can contain same-chain duplicates. The old key
    // only serves as a fallback for states that never saw the new
    // format. Persistence is suppressed while a restore runs
    // (persistPluginChain + isChainRestoreInProgress), so a mid-restore
    // echo can no longer produce a stale empty pluginChains either.
    if (newVar.isObject())
        return newVar;
    return oldVar;
}

void BluePrinterAudioProcessor::restoreUserState()
{
    auto* props = getUserState();
    if (props == nullptr)
        return;

    // Freshness anchor for the crash diagnostics (readFreshCrashOpDetail):
    // the properties file's mtime before this session writes anything.
    // The app is the only writer of both files, so a crash-info.txt
    // newer than this was written by the launch that just crashed.
    userStateMtimeAtLaunch = props->getFile().getLastModificationTime();

    // 1. Library folder. Setting it auto-loads any .wav sidecars.
    const auto folderPath = props->getValue ("libraryFolder");
    if (folderPath.isNotEmpty())
    {
        juce::File folder (folderPath);
        if (folder.isDirectory())
        {
            // setLibraryFolder also calls persistLibraryFolder(), which
            // is a no-op write here (we just read the same value back).
            setLibraryFolder (folder);
        }
        else
        {
            // The folder was moved or deleted since the last run. Keep
            // the stale path in the file so the user can see what they
            // had, but don't try to load it.
            {
                juce::ScopedLock lock (libraryFolderLock);
                libraryFolder = folder;
            }
        }
    }

    // 3. User tag names for the snippet colours. Stored as a JSON
    //    object keyed by colour key; empty names are dropped.
    const auto tagsJson = props->getValue ("tagNames");
    if (tagsJson.isNotEmpty())
    {
        const auto tagsVar = juce::JSON::parse (tagsJson);
        if (auto* tagsObj = tagsVar.getDynamicObject())
        {
            for (const auto& entry : tagsObj->getProperties())
                if (entry.value.isString() && entry.value.toString().trim().isNotEmpty())
                    tagNames[entry.name.toString()] = entry.value.toString().trim();
        }
    }

    // 4. VST3 chains. Guarded so the addPlugin calls inside don't
    // trigger a redundant write back to the file. The bundle holds
    // the chain slots plus the shared library (blocklist + cached
    // scan). loadSavedChainState picks whichever saved key actually
    // holds chain content.
    const auto chainVar = loadSavedChainState();
    if (chainVar.isObject())
    {
        persistingPluginChain.store (true, std::memory_order_release);
        juce::String error;
        applyChainState (chainVar, error);
        persistingPluginChain.store (false, std::memory_order_release);
        if (error.isNotEmpty())
            lastChainRestoreError = error;
    }
}

void BluePrinterAudioProcessor::restoreSavedPluginChains()
{
    if (pluginChainsRestored)
        return;

    pluginChainsRestored = true;

    const auto chainVar = loadSavedChainState();
    if (! chainVar.isObject())
        return;

    persistingPluginChain.store (true, std::memory_order_release);
    juce::String error;
    applyChainState (chainVar, error);
    persistingPluginChain.store (false, std::memory_order_release);
    if (error.isNotEmpty())
        lastChainRestoreError = error;
}

void BluePrinterAudioProcessor::persistLibraryFolder()
{
    if (auto* props = getUserState())
    {
        juce::ScopedLock lock (libraryFolderLock);
        props->setValue ("libraryFolder", libraryFolder.getFullPathName());
        props->saveIfNeeded();
    }
}

bool BluePrinterAudioProcessor::isChainRestoreInProgress() const
{
    if (restoreActive)
        return true;
    if (pendingPluginLoads.load (std::memory_order_acquire) != 0)
        return true;
    // A state blob queued by the load callback but not yet applied
    // (applied on the next timer tick): while it waits, the in-memory
    // chains would serialize the plugin's defaults instead of its
    // saved state, so treat the restore as still running.
    if (pendingStateApply != nullptr)
        return true;
    const juce::ScopedLock sl (chainLock);
    for (const auto& chain : chains)
        if (chain->hasPendingSlots())
            return true;
    return false;
}

void BluePrinterAudioProcessor::persistPluginChain()
{
    // Safe to call from any thread: hosted plugins notify parameter
    // changes from their audio thread (PluginChain's
    // AudioProcessorListener forwards straight here), so this only
    // touches atomics. The expensive serialize + disk write happens on
    // the message thread in flushPendingChainPersist (via timerCallback).
    if (persistingPluginChain.load (std::memory_order_acquire))
        return; // restore in progress, don't echo back
    // A real mutation means the user has seen and accepted the current
    // chain state (which may hold default plugin states after a self-
    // heal launch); from here on the exit capture includes the chains
    // again (see stateRestoreSkippedThisLaunch / getStateInformation).
    stateRestoreSkippedThisLaunch.store (false, std::memory_order_relaxed);
    // Debounced: the actual save (serializing every plugin's state and
    // writing the file) happens once, 500 ms after the last mutation —
    // see flushPendingChainPersist and timerCallback. Arming is safe
    // even while the deferred restore is still loading: the flush
    // itself refuses to write until the restore has fully completed,
    // and keeps the arm pending so a mutation made mid-restore (e.g.
    // the user adds a plugin while the saved ones are still loading)
    // is persisted as soon as the restore finishes instead of being
    // silently dropped.
    chainPersistPending.store (true, std::memory_order_release);
    chainPersistDeadline.store (juce::Time::currentTimeMillis() + 500,
                                std::memory_order_release);
}

void BluePrinterAudioProcessor::flushPendingChainPersist()
{
    if (! chainPersistPending.load (std::memory_order_acquire))
        return;
    // While the deferred restore is still loading saved slots, the
    // in-memory chains are partial (or empty); writing now would
    // replace the good state on disk with the partial one. Keep the
    // arm pending instead of dropping it — the timer retries every
    // tick, so once the restore completes the pending save writes the
    // complete state (including any mid-restore user mutations).
    if (isChainRestoreInProgress())
        return;
    chainPersistPending.store (false, std::memory_order_release);
    if (auto* props = getUserState())
    {
        props->setValue ("pluginChains",
                         juce::JSON::toString (makeChainState(), false));
        props->saveIfNeeded();
    }
}
