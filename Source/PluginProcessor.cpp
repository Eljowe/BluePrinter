/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebViewEditor.h"

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
    // persistPluginChain.
    createChain ("MIDI Chain", ChainInputBoth, true, true);
    createChain ("Audio FX Chain", ChainInputBoth, false, true);

    // Wire the chain persistence AFTER the default chains are seeded so
    // the startup write only happens if the user actually mutates a
    // chain. Every chain shares one persistence callback because the
    // save format is a single bundle containing all of them.
    for (auto& chain : chains)
        chain->onChanged = [this] { persistPluginChain(); };
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

    // Apply gain. This is the post-DSP signal we want to record and the
    // pass-through signal when nothing else is happening.
    const auto gain = apvts.getRawParameterValue ("Gain")->load();
    for (int channel = 0; channel < numChannels; ++channel)
        buffer.applyGain (channel, 0, numSamples, gain);

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

    // 1c. Scale the direct dry pass-through by the Dry level. Chains
    //    still receive the full input (chainInputBuffer was copied
    //    above); this only controls how much raw dry is heard in the
    //    mix — turn it to zero and only the chains are audible.
    {
        const float dry = dryLevel.load (std::memory_order_acquire);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.applyGain (ch, 0, numSamples, dry);
    }

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
    //    (scaled by the Dry level so captures match what you hear) and
    //    accumulates only the chains whose record toggle is on — that
    //    is what captures (take recorder + looper) record.
    recordingMixBuffer.clear();
    {
        const int mixCh = juce::jmin (numChannels, recordingMixBuffer.getNumChannels());
        for (int ch = 0; ch < mixCh; ++ch)
            recordingMixBuffer.copyFrom (ch, 0, chainInputBuffer, ch, 0, numSamples);
    }
    recordingMixBuffer.applyGain (dryLevel.load (std::memory_order_acquire));

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

        // Output volume + mute. Muted chains still run (their plugins
        // keep internal state consistent) but contribute nothing to the
        // mix or the capture.
        const float gain = chain->isMuted()
            ? 0.0f
            : juce::Decibels::decibelsToGain (chain->getVolumeDb());

        for (int ch = 0; ch < sumCh; ++ch)
            buffer.addFrom (ch, 0, chainScratchBuffer, ch, 0, numSamples, gain);

        if (chain->isRecordOnCapture() && gain > 0.0f)
        {
            const int mixCh = juce::jmin (recordingMixBuffer.getNumChannels(), sumCh);
            for (int ch = 0; ch < mixCh; ++ch)
                recordingMixBuffer.addFrom (ch, 0, chainScratchBuffer, ch, 0, numSamples, gain);
        }

        // Per-chain output meter (post-volume; muted chains read 0).
        // Smoothed against the chain's previous values, like the main
        // input meter.
        computeLevelsInto (chainScratchBuffer, numSamples,
                           chain->outputLevel, chain->outputPeak, gain);
    }

    // 3. Looper capture: tap the record mix so the loop bakes in
    //    whatever the selected chains produce (synth sounds, FX).
    //    Deliberately before the click is mixed in so the click never
    //    ends up in the loop. Uses the same pre-allocated recordBuffer
    //    as the take recorder.
    if (looperCaptureArmed.load (std::memory_order_acquire))
    {
        const auto writePos = audioLoopLength.load (std::memory_order_relaxed);
        const auto toCopy = juce::jmin (numSamples, maxRecordSamples - static_cast<int> (writePos));
        if (toCopy > 0)
        {
            for (int ch = 0; ch < juce::jmin (numChannels, recordBuffer->getNumChannels()); ++ch)
                recordBuffer->copyFrom (ch, static_cast<int> (writePos), recordingMixBuffer, ch, 0, toCopy);
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

    // 5. Compute input levels from the still-clean signal so the click
    //    doesn't pump the meter.
    computeLevels (buffer, numSamples);

    // 6. Audio loop playback. Runs after the chains so the already-
    //    processed loop audio isn't re-processed (it was captured
    //    post-chain). Mixed over the live input rather than replacing
    //    it, so you can play over the loop. Reads the cropped window
    //    [audioLoopStart, audioLoopStart + audioLoopLength).
    if (audioLoopPlaying.load (std::memory_order_acquire))
    {
        const auto start = audioLoopStart.load (std::memory_order_acquire);
        const auto length = audioLoopLength.load (std::memory_order_acquire);
        auto position = audioLoopPosition.load (std::memory_order_acquire);
        if (length > 0 && recordBuffer != nullptr)
        {
            const auto toCopy = juce::jmin (numSamples, static_cast<int> (length - position));
            const auto channels = juce::jmin (numChannels, recordBuffer->getNumChannels());
            const auto crossfade = juce::jmin (loopCrossfadeSamples, static_cast<int> (length / 2));
            for (int ch = 0; ch < channels; ++ch)
            {
                buffer.addFrom (ch, 0, *recordBuffer, ch, static_cast<int> (start + position), toCopy);
                if (crossfade > 0 && position + toCopy >= length)
                {
                    const auto fadeStart = juce::jmax<int64_t> (0, length - crossfade);
                    const auto overlapStart = juce::jmax<int64_t> (0, position - fadeStart);
                    const auto overlapLength = juce::jmin (crossfade - static_cast<int> (overlapStart), toCopy);
                    for (int i = 0; i < overlapLength; ++i)
                    {
                        const auto loopIndex = position + i;
                        const auto fade = static_cast<float> (loopIndex - fadeStart) / static_cast<float> (crossfade);
                        const auto endSample = recordBuffer->getSample (ch, static_cast<int> (start + loopIndex));
                        const auto startSample = recordBuffer->getSample (ch, static_cast<int> (start + i));
                        buffer.addSample (ch, i, (startSample - endSample) * fade);
                    }
                }
            }
            position += toCopy;
            if (position >= length)
                position = looperLooping.load (std::memory_order_acquire) ? 0 : length;
            audioLoopPosition.store (position, std::memory_order_release);
            if (! looperLooping.load (std::memory_order_acquire) && position >= length)
                audioLoopPlaying.store (false, std::memory_order_release);
        }
    }

    // 7. Playback overwrites the output buffer. Done after recording so
    //    monitoring of the input stops while a snippet is playing.
    if (playbackActive.load (std::memory_order_acquire))
        renderPlayback (buffer, numSamples);

    // 8. Looper count-in: play the click, advance the beat clock, and flip
    //    into capture once the configured beats have elapsed. Mirrors the
    //    take-recorder pre-roll below but drives the looper's own capture
    //    state. Rendered post-chain so the click is at the same level and
    //    colour as the take recorder's.
    if (looperPreRollActive.load (std::memory_order_acquire))
    {
        const int64_t startPos = metronomePosition.load (std::memory_order_acquire);
        if (looperMetronomeEnabled.load (std::memory_order_acquire))
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
            audioLoopLength.store (0, std::memory_order_release);
            // Capture is starting: re-sync external gear when the clock
            // is already running from another source (the global toggle).
            // The looper's own toggle already started the clock when the
            // count-in began.
            if (clockRunning.load (std::memory_order_acquire)
                && ! looperMidiClockEnabled.load (std::memory_order_acquire))
                midiStartPending.store (true, std::memory_order_release);
        }
        metronomePosition.store (newPos, std::memory_order_release);
        transportPosition.store (newPos, std::memory_order_release);
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
            beginActualRecording();
        }
        else
        {
            transportPosition.store (newPos, std::memory_order_release);
            metronomePosition.store (newPos, std::memory_order_release);
        }
    }

    // 10. Click during recording. The metronome beat clock runs
    //    continuously from the recording start (or count-in end) so
    //    beats land at evenly-spaced positions regardless of when the
    //    recording was started. The clock keeps advancing even when
    //    the metronome is muted, so toggling the metronome back on
    //    doesn't shift the beat grid. clickDuringTake off = the click
    //    only plays during the count-in, never through the take.
    if (recordingRequested.load (std::memory_order_acquire))
    {
        const int64_t startPos = metronomePosition.load (std::memory_order_acquire);
        if (metronomeEnabled.load (std::memory_order_acquire)
            && clickDuringTake.load (std::memory_order_acquire))
            renderMetronomeInBlock (buffer, startPos, numSamples);

        const int64_t newPos = startPos + numSamples;
        metronomePosition.store (newPos, std::memory_order_release);
        transportPosition.store (newPos, std::memory_order_release);
    }

    // 11. Click during looper capture. Same beat clock, so the looper's
    //    count-in flows straight into capture with evenly spaced beats.
    //    Mixed after the capture tap so the click never lands in the loop.
    if (looperCaptureArmed.load (std::memory_order_acquire))
    {
        const int64_t startPos = metronomePosition.load (std::memory_order_acquire);
        if (looperMetronomeEnabled.load (std::memory_order_acquire))
            renderMetronomeInBlock (buffer, startPos, numSamples);

        const int64_t newPos = startPos + numSamples;
        metronomePosition.store (newPos, std::memory_order_release);
        transportPosition.store (newPos, std::memory_order_release);
    }

    // 12. MIDI clock output. Clock pulses (0xF8) are generated at
    //    24 ppqn from the continuous metronomePosition so they align
    //    with the audible metronome and run through count-in into the
    //    recording. The clock runs when the global toggle is on or a
    //    per-section toggle's operation is active. When the global
    //    toggle alone keeps it alive, the clock advances the position
    //    itself so it runs free — the user can drive a drum machine's
    //    presets without recording, and the audible click plays along
    //    so the beats can be heard (subject to the metronome toggle).
    //    Queued MIDI Start / Stop are flushed here so the receiver
    //    gets them at a block boundary.
    {
        refreshClockRunning();

        const bool clockEnabled = clockRunning.load (std::memory_order_acquire);
        const bool clockFreeRan = clockEnabled
            && metronomePosition.load (std::memory_order_acquire) == blockStartMetronomePos;

        if (clockFreeRan)
        {
            metronomePosition.store (blockStartMetronomePos + numSamples,
                                     std::memory_order_release);

            // Sound the click for the free-running clock so the beats
            // are audible without recording or looping. Only the global
            // toggle free-runs: the per-section toggles run while their
            // operation is active, and those operations decide their own
            // click (take click toggle, looper click toggle).
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

void BluePrinterAudioProcessor::renderMetronomeInBlock (juce::AudioBuffer<float>& buffer,
                                                        int64_t startPos,
                                                        int numSamples)
{
    // Copy the shared_ptrs once per block so a message-thread
    // resynthesizeClicks() (click sound settings changed) can never
    // invalidate the buffers mid-render.
    const auto normalClick = clickBuffer;
    const auto accentClick = accentClickBuffer;
    if ((normalClick == nullptr || normalClick->empty())
     && (accentClick == nullptr || accentClick->empty()))
        return;

    const double bpmValue = bpm.load (std::memory_order_acquire);
    if (bpmValue <= 0.0 || currentSampleRate <= 0.0)
        return;

    const double samplesPerBeat = 60.0 / bpmValue * currentSampleRate;
    if (samplesPerBeat <= 0.0)
        return;

    const int numChannels = buffer.getNumChannels();

    // Accent the first beat of every bar — beats whose index is a
    // multiple of countInBeats (default 4). Falls back to 4-beat bars
    // when count-in is disabled so the accent still works during plain
    // recording. The accent uses its own brighter, louder click; the
    // other beats use the softer tick.
    const int beatsPerBar = juce::jmax (1, countInBeats.load (std::memory_order_acquire));

    // Beat boundaries that fall inside [startPos, startPos + numSamples).
    const int64_t endPos = startPos + numSamples;
    const int firstBeat  = static_cast<int> (std::ceil (static_cast<double> (startPos) / samplesPerBeat));
    const int lastBeat   = static_cast<int> (std::floor (static_cast<double> (endPos)   / samplesPerBeat));

    for (int beat = firstBeat; beat <= lastBeat; ++beat)
    {
        const int64_t beatSample = static_cast<int64_t> (beat * samplesPerBeat);
        const int blockOffset = static_cast<int> (beatSample - startPos);
        if (blockOffset < 0 || blockOffset >= numSamples)
            continue;

        const std::vector<float>* click = normalClick.get();
        if (beat % beatsPerBar == 0 && accentClick != nullptr && ! accentClick->empty())
            click = accentClick.get();
        if (click == nullptr || click->empty())
            continue;

        const int clickLen = static_cast<int> (click->size());
        const int remaining = juce::jmin (clickLen, numSamples - blockOffset);
        for (int j = 0; j < remaining; ++j)
        {
            const float sample = (*click)[static_cast<size_t> (j)];
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addSample (ch, blockOffset + j, sample);
        }
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

    for (int ch = 0; ch < channels; ++ch)
    {
        destination.copyFrom (ch, 0, audio, ch, readPos, toCopy);
    }

    // Apply playback volume so the user can balance playback against
    // their live input. The input gain (applied earlier in
    // processBlock) is separate — this only scales the rendered
    // snippet audio.
    {
        const auto playbackVol = apvts.getRawParameterValue ("PlaybackVolume")->load();
        for (int ch = 0; ch < channels; ++ch)
            destination.applyGain (ch, 0, toCopy, playbackVol);
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

void BluePrinterAudioProcessor::computeLevelsInto (const juce::AudioBuffer<float>& source,
                                                   int numSamples,
                                                   std::atomic<float>& levelAtomic,
                                                   std::atomic<float>& peakAtomic,
                                                   float gain)
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
}

void BluePrinterAudioProcessor::computeLevels (const juce::AudioBuffer<float>& source, int numSamples)
{
    computeLevelsInto (source, numSamples, inputLevel, inputPeak, 1.0f);
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

        looperPreRollActive.store (false, std::memory_order_release);
        looperCaptureArmed.store (false, std::memory_order_release);
        audioLoopRecording.store (false, std::memory_order_release);
        audioLoopPlaying.store (false, std::memory_order_release);
        audioLoopStart.store (0, std::memory_order_release);
        audioLoopLength.store (0, std::memory_order_release);
        audioLoopPosition.store (0, std::memory_order_release);
        looperCropStartBars = 0;
        looperCropEndBars = 0;
        looperPeaks.clear();

        if (looperMetronomeEnabled.load (std::memory_order_acquire)
            && looperCountInBeats.load (std::memory_order_acquire) > 0)
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
            // gear if the clock is already running from another source
            // (the free-running global toggle). The looper's own toggle
            // starts the clock via updateClockRunState below.
            if (clockRunning.load (std::memory_order_acquire)
                && ! looperMidiClockEnabled.load (std::memory_order_acquire))
                midiStartPending.store (true, std::memory_order_release);
        }
    }
    else
    {
        looperPreRollActive.store (false, std::memory_order_release);
        looperCaptureArmed.store (false, std::memory_order_release);
        audioLoopRecording.store (false, std::memory_order_release);
        trimLooperToMusicalGrid();
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

    const auto start = audioLoopStart.load (std::memory_order_acquire);
    const auto length = audioLoopLength.load (std::memory_order_acquire);
    if (recordBuffer == nullptr || length <= 0)
        return;

    // Copy the cropped window out under the lock (guards against the take
    // recorder writing concurrently) and downsample for the UI.
    juce::AudioBuffer<float> region (recordBuffer->getNumChannels(), static_cast<int> (length));
    {
        const juce::ScopedLock sl (recordLock);
        for (int ch = 0; ch < region.getNumChannels(); ++ch)
            region.copyFrom (ch, 0, *recordBuffer, ch, static_cast<int> (start), static_cast<int> (length));
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
    auto target = static_cast<int64_t> (std::llround (static_cast<double> (captured) / bar) * bar);
    if (target <= 0)
        target = static_cast<int64_t> (std::llround (static_cast<double> (captured) / beat) * beat);
    target = juce::jlimit<int64_t> (1, captured, target);
    audioLoopStart.store (0, std::memory_order_release);
    audioLoopLength.store (target, std::memory_order_release);
    looperCropStartBars = 0;
    looperCropEndBars = 0;
    refreshLooperPeaks();
}

int BluePrinterAudioProcessor::addLoopSnippet()
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
    return snippet->id;
}

void BluePrinterAudioProcessor::setLooperPlaying (bool enabled)
{
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

void BluePrinterAudioProcessor::setLooperClickEnabled (bool enabled)
{
    looperMetronomeEnabled.store (enabled);
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setLooperCountInBeats (int beats)
{
    looperCountInBeats.store (juce::jlimit (0, 8, beats));
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setLoopCrop (int startBars, int endBars)
{
    const auto length = audioLoopLength.load (std::memory_order_acquire);
    if (length <= 0 || currentSampleRate <= 0.0)
        return;

    const auto beat = 60.0 * currentSampleRate / juce::jmax (1.0f, bpm.load());
    const auto bar = beat * 4.0;
    const auto loopBars = juce::jmax (1, static_cast<int> (std::llround (static_cast<double> (length) / bar)));

    startBars = juce::jlimit (0, loopBars - 1, startBars);
    endBars = juce::jlimit (0, loopBars - 1 - startBars, endBars);
    looperCropStartBars = startBars;
    looperCropEndBars = endBars;

    const auto trimStart = static_cast<int64_t> (startBars * bar);
    const auto trimEnd = static_cast<int64_t> (endBars * bar);
    audioLoopStart.store (trimStart, std::memory_order_release);
    audioLoopLength.store (length - trimStart - trimEnd, std::memory_order_release);

    // Keep the playhead inside the cropped window.
    const auto remaining = audioLoopLength.load (std::memory_order_acquire);
    audioLoopPosition.store (juce::jmin (audioLoopPosition.load (std::memory_order_acquire),
                                         juce::jmax<int64_t> (0, remaining - 1)),
                             std::memory_order_release);
    if (remaining <= 0)
        audioLoopPlaying.store (false, std::memory_order_release);

    refreshLooperPeaks();
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::clearLoop()
{
    looperPreRollActive.store (false, std::memory_order_release);
    looperCaptureArmed.store (false, std::memory_order_release);
    audioLoopRecording.store (false, std::memory_order_release);
    audioLoopPlaying.store (false, std::memory_order_release);
    audioLoopStart.store (0, std::memory_order_release);
    audioLoopLength.store (0, std::memory_order_release);
    audioLoopPosition.store (0, std::memory_order_release);
    looperCropStartBars = 0;
    looperCropEndBars = 0;
    looperPeaks.clear();
    updateClockRunState();
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::beginActualRecording()
{
    {
        const juce::ScopedLock sl (recordLock);
        recordBuffer->clear();
        recordWritePos.store (0, std::memory_order_release);
        recordingState.store (RecordingState::Recording, std::memory_order_release);
        recordingFinalizePending.store (false, std::memory_order_release);
    }
    recordingRequested.store (true, std::memory_order_release);
    transportPosition.store (0, std::memory_order_release);

    // Re-sync external gear when the clock is already running from a
    // source other than the take itself (e.g. the free-running global
    // toggle): fire Start again so the drum machine restarts its pattern
    // on the take's first beat. When the take itself drives the clock
    // (takeMidiClockEnabled), the machine was already started at the
    // count-in and stays in sync — no restart mid-count-in.
    if (clockRunning.load (std::memory_order_acquire)
        && ! takeMidiClockEnabled.load (std::memory_order_acquire))
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

void BluePrinterAudioProcessor::setDryLevel (float level)
{
    const float clamped = juce::jlimit (0.0f, 1.0f, level);
    if (dryLevel.load (std::memory_order_acquire) == clamped)
        return;
    dryLevel.store (clamped, std::memory_order_release);
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

    // Recompute the clock state from all sources: the global toggle
    // alone can free-run the clock, or the take / looper toggles can
    // drive it while their operation is active.
    updateClockRunState();

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

// The clock runs when the global toggle is on, or when a per-section
// toggle is on and its operation is active (take: count-in or recording;
// looper: count-in, capture or playback). Edges send Start / Stop
// directly to the hardware and queue them for the host buffer.
void BluePrinterAudioProcessor::updateClockRunState()
{
    const bool takeActive = preRollActive.load (std::memory_order_acquire)
                         || recordingRequested.load (std::memory_order_acquire);
    const bool looperActive = looperPreRollActive.load (std::memory_order_acquire)
                           || looperCaptureArmed.load (std::memory_order_acquire)
                           || audioLoopPlaying.load (std::memory_order_acquire);

    const bool wantRun = midiClockEnabled.load (std::memory_order_acquire)
                      || (takeMidiClockEnabled.load (std::memory_order_acquire) && takeActive)
                      || (looperMidiClockEnabled.load (std::memory_order_acquire) && looperActive);

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
        // Only close the device once no source can request the clock
        // anymore — the global toggle is the only persistent source.
        if (! midiClockEnabled.load (std::memory_order_acquire))
            closeMidiOutputDevice();
    }
}

void BluePrinterAudioProcessor::refreshClockRunning()
{
    const bool takeActive = preRollActive.load (std::memory_order_acquire)
                         || recordingRequested.load (std::memory_order_acquire);
    const bool looperActive = looperPreRollActive.load (std::memory_order_acquire)
                           || looperCaptureArmed.load (std::memory_order_acquire)
                           || audioLoopPlaying.load (std::memory_order_acquire);

    const bool wantRun = midiClockEnabled.load (std::memory_order_acquire)
                      || (takeMidiClockEnabled.load (std::memory_order_acquire) && takeActive)
                      || (looperMidiClockEnabled.load (std::memory_order_acquire) && looperActive);

    const bool wasRunning = clockRunning.exchange (wantRun, std::memory_order_acq_rel);
    if (wantRun == wasRunning)
        return;

    // Edge detected on the audio thread (e.g. a one-shot loop finished or
    // the max-length recording buffer filled). Device open/close stays on
    // the message thread; just command the transport here so the drum
    // machine stops in time.
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

void BluePrinterAudioProcessor::setTakeMidiClockEnabled (bool enabled)
{
    takeMidiClockEnabled.store (enabled, std::memory_order_release);
    updateClockRunState(); // applies immediately if a take is in progress
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setLooperMidiClockEnabled (bool enabled)
{
    looperMidiClockEnabled.store (enabled, std::memory_order_release);
    updateClockRunState(); // applies immediately if the looper is active
    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::setClickDuringTake (bool enabled)
{
    clickDuringTake.store (enabled, std::memory_order_release);
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
        restoreStateBlobs = ! props->getBoolValue ("chainRestoreCrashed", false);
        props->setValue ("chainRestoreCrashed", true);
        props->saveIfNeeded();
    }
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
    const bool wasPersisting = persistingPluginChain;
    persistingPluginChain = true;

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

    // The whole restore completed without crashing — clear the crash
    // marker so the next launch restores saved plugin states again.
    if (auto* props = getUserState())
    {
        props->setValue ("chainRestoreCrashed", false);
        props->saveIfNeeded();
    }

    restoreActive = false;

    persistingPluginChain = wasPersisting;
}

void BluePrinterAudioProcessor::clearLastChainRestoreError()
{
    lastChainRestoreError.clear();
}

//==============================================================================
void BluePrinterAudioProcessor::timerCallback()
{
    // Flush the debounced chain save once it has been quiet for 500 ms.
    if (chainPersistPending && juce::Time::currentTimeMillis() >= chainPersistDeadline)
        flushPendingChainPersist();

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
            if (slot.file.existsAsFile())
            {
                pendingPluginLoads.store (1, std::memory_order_release);
                const auto chainId = target->getChainId();
                target->addPluginAsync (slot.file, 10000,
                    [this, chainId, slot = std::move (slot)] (int slotIndex,
                                                              const juce::String&,
                                                              const juce::String&,
                                                              bool) mutable
                    {
                        pendingPluginLoads.store (0, std::memory_order_release);
                        auto* chain = getChainById (chainId);
                        if (chain == nullptr || slotIndex < 0)
                            return;

                        chain->setBypass (slotIndex, slot.bypassed);

                        // Apply the saved state blob. Bypassed slots and
                        // self-healing mode (a previous launch crashed
                        // mid-restore) skip it.
                        if (! slot.bypassed && slot.stateBase64.isNotEmpty()
                            && ! vst3Library.getSkipStateRestore())
                        {
                            juce::MemoryBlock stateData;
                            if (stateData.fromBase64Encoding (slot.stateBase64))
                            {
                                if (auto* plugin = chain->getPlugin (slotIndex))
                                {
                                    BluePrinterAudioProcessor::setCrashOp ("restoring plugin state (setStateInformation)", slot.file.getFileName().toRawUTF8());
                                    plugin->setStateInformation (stateData.getData(),
                                                                 static_cast<int> (stateData.getSize()));
                                }
                            }
                        }

                        listeners.call ([](Listener& l) { l.pluginChainChanged(); });
                    });
            }
        }
    }

    if (recordingFinalizePending.exchange (false, std::memory_order_acq_rel))
        finalizeRecordingOnMessageThread();

    // Peak meter decay.
    const float prevPeak = inputPeak.load (std::memory_order_acquire);
    if (prevPeak > 0.001f)
        inputPeak.store (prevPeak * 0.92f, std::memory_order_release);

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::finalizeRecordingOnMessageThread()
{
    // The take ended (possibly by filling the max-length buffer on the
    // audio thread): release the clock if no other source wants it.
    updateClockRunState();

    if (recordBuffer == nullptr)
        return;

    std::shared_ptr<Snippet> snippet;

    {
        const juce::ScopedLock sl (recordLock);
        const int captured = static_cast<int> (recordWritePos.load (std::memory_order_acquire));
        if (captured <= 0)
        {
            recordWritePos.store (0, std::memory_order_release);
            return;
        }

        const int channels = recordBuffer->getNumChannels();
        auto snippetBuffer = std::make_shared<juce::AudioBuffer<float>> (channels, captured);
        for (int ch = 0; ch < channels; ++ch)
            snippetBuffer->copyFrom (ch, 0, *recordBuffer, ch, 0, captured);

        recordWritePos.store (0, std::memory_order_release);

        auto defaultName = "Snippet " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");
        snippet = library.addSnippet (snippetBuffer, getSampleRate(), defaultName);
    }

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
    state.setProperty ("dryLevel",         dryLevel.load(),         nullptr);
    state.setProperty ("midiClockEnabled", midiClockEnabled.load(), nullptr);
    state.setProperty ("takeMidiClock",    takeMidiClockEnabled.load(), nullptr);
    state.setProperty ("looperMidiClock",  looperMidiClockEnabled.load(), nullptr);
    state.setProperty ("clickDuringTake",  clickDuringTake.load(), nullptr);
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
            dryLevel.store         (static_cast<float> (state.getProperty ("dryLevel",         1.0f)));
            midiClockEnabled.store (static_cast<bool>  (state.getProperty ("midiClockEnabled", false)));
            takeMidiClockEnabled.store (static_cast<bool> (state.getProperty ("takeMidiClock",   false)));
            looperMidiClockEnabled.store (static_cast<bool> (state.getProperty ("looperMidiClock", false)));
            clickDuringTake.store (static_cast<bool> (state.getProperty ("clickDuringTake", true)));
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
        // output device so external gear picks up right away. This is
        // deferred to the message thread because MidiOutput::openDevice
        // must run there.
        if (midiClockEnabled.load (std::memory_order_acquire))
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

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "Gain",
        "Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.7f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "PlaybackVolume",
        "Playback Volume",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.8f));

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

    // Count how many plugin slots a saved bundle actually holds, across
    // every format (new "chains" array, legacy midiChain/audioChain
    // split, and the pre-split single "slots" array).
    auto countSlots = [](const juce::var& v) -> int
    {
        if (! v.isObject())
            return 0;
        auto* obj = v.getDynamicObject();

        int slots = 0;
        if (auto* chainsArr = obj->getProperty ("chains").getArray())
        {
            for (const auto& c : *chainsArr)
                if (auto* co = c.getDynamicObject())
                    if (auto* s = co->getProperty ("slots").getArray())
                        slots += s->size();
            return slots;
        }
        for (const char* key : { "midiChain", "audioChain" })
            if (auto* co = obj->getProperty (key).getDynamicObject())
                if (auto* s = co->getProperty ("slots").getArray())
                    slots += s->size();
        if (auto* s = obj->getProperty ("slots").getArray())
            slots += s->size();
        return slots;
    };

    // Prefer whichever key actually holds chain content. A newer
    // pluginChains that is empty or stale (e.g. the "chains": [] echo a
    // pre-fix restore wrote) must not shadow the older, valid
    // pluginChain. Ties favour the new format.
    if (countSlots (newVar) > 0)
        return newVar;
    if (countSlots (oldVar) > 0)
        return oldVar;
    return newVar.isObject() ? newVar : oldVar;
}

void BluePrinterAudioProcessor::restoreUserState()
{
    auto* props = getUserState();
    if (props == nullptr)
        return;

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

    // 2. VST3 chains. Guarded so the addPlugin calls inside don't
    // trigger a redundant write back to the file. The bundle holds
    // the chain slots plus the shared library (blocklist + cached
    // scan). loadSavedChainState picks whichever saved key actually
    // holds chain content.
    const auto chainVar = loadSavedChainState();
    if (chainVar.isObject())
    {
        persistingPluginChain = true;
        juce::String error;
        applyChainState (chainVar, error);
        persistingPluginChain = false;
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

    persistingPluginChain = true;
    juce::String error;
    applyChainState (chainVar, error);
    persistingPluginChain = false;
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

void BluePrinterAudioProcessor::persistPluginChain()
{
    if (persistingPluginChain)
        return; // restore in progress, don't echo back
    // Debounced: the actual save (serializing every plugin's state and
    // writing the file) happens once, 500 ms after the last mutation —
    // see flushPendingChainPersist and timerCallback.
    chainPersistPending = true;
    chainPersistDeadline = juce::Time::currentTimeMillis() + 500;
}

void BluePrinterAudioProcessor::flushPendingChainPersist()
{
    if (! chainPersistPending)
        return;
    chainPersistPending = false;
    if (auto* props = getUserState())
    {
        props->setValue ("pluginChains",
                         juce::JSON::toString (makeChainState(), false));
        props->saveIfNeeded();
    }
}
