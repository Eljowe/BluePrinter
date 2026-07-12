/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebViewEditor.h"

#include <thread>

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
    // Restore the standalone user state (library folder + VST3 chain)
    // before any UI is built. setLibraryFolder auto-loads snippets from
    // the folder; setChainState replays the saved chain. Both fire
    // their own change events; that's fine because the chain's
    // onChanged is wired below in restoreUserState()'s tail, after the
    // restore itself completes.
    restoreUserState();

    // Wire the chain persistence AFTER the restore so we don't write
    // the just-loaded state back over the file on startup.
    pluginChain.onChanged = [this] { persistPluginChain(); };
}

BluePrinterAudioProcessor::~BluePrinterAudioProcessor()
{
    stopTimer();
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
    juce::ignoreUnused (samplesPerBlock);

    const int channels = juce::jmax (1, getTotalNumInputChannels());
    const auto maxSamples = static_cast<int> (sampleRate * maxRecordingSeconds);

    recordBuffer = std::make_unique<juce::AudioBuffer<float>> (channels, maxSamples);
    recordBuffer->clear();
    maxRecordSamples = maxSamples;
    recordWritePos.store (0, std::memory_order_release);

    currentSampleRate = sampleRate;

    // Hand the new rate/block size to the FX chain so every loaded plugin
    // is prepared with the right values.
    pluginChain.prepareToPlay (sampleRate, samplesPerBlock);

    // Synthesize a 50 ms percussive click: fundamental 800 Hz + a couple of
    // harmonics, fast exponential decay. Single-channel, mixed into all
    // output channels.
    const double clickDuration = 0.05;
    const int clickSamples = juce::jmax (1, static_cast<int> (sampleRate * clickDuration));
    clickBuffer.assign (static_cast<size_t> (clickSamples), 0.0f);
    for (int i = 0; i < clickSamples; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (sampleRate);
        const float envelope = std::exp (-t * 80.0f);
        float s = 0.0f;
        s += std::sin (2.0f * juce::MathConstants<float>::twoPi *  800.0f * t) * 0.55f;
        s += std::sin (2.0f * juce::MathConstants<float>::twoPi * 1600.0f * t) * 0.30f;
        s += std::sin (2.0f * juce::MathConstants<float>::twoPi * 2400.0f * t) * 0.15f;
        clickBuffer[static_cast<size_t> (i)] = s * envelope * 0.40f;
    }

    startTimerHz (transportTimerHz);
}

void BluePrinterAudioProcessor::releaseResources()
{
    stopTimer();
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

    pluginChain.releaseResources();
    clickBuffer.clear();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool BluePrinterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
   #endif

    return true;
  #endif
}
#endif

void BluePrinterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Apply gain. This is the post-DSP signal we want to record and the
    // pass-through signal when nothing else is happening.
    const auto gain = apvts.getRawParameterValue ("Gain")->load();
    for (int channel = 0; channel < numChannels; ++channel)
        buffer.applyGain (channel, 0, numSamples, gain);

    // 0. Run the VST3 FX chain in place. Done after the input gain so
    //    the chain processes the post-gain signal; done before the
    //    recording tap so the recorded file captures the processed
    //    signal. Each plugin is responsible for matching the buffer's
    //    channel layout.
    pluginChain.processBlock (buffer, midiMessages);

    // 1. Record the clean (post-gain, pre-click) input. Access to the
    //    record buffer is serialised with the message thread via recordLock.
    {
        const juce::ScopedLock sl (recordLock);
        if (recordingRequested.load (std::memory_order_acquire))
            writeRecording (buffer, numSamples);
    }

    // 2. Compute input levels from the still-clean signal so the click
    //    doesn't pump the meter.
    computeLevels (buffer, numSamples);

    // 3. Playback overwrites the output buffer. Done after recording so
    //    monitoring of the input stops while a snippet is playing.
    if (playbackActive.load (std::memory_order_acquire))
        renderPlayback (buffer, numSamples);

    // 4. Pre-roll (count-in): add the click to the output, advance the
    //    position, and flip into recording once the configured number of
    //    beats has elapsed. The transition is deferred to the next block
    //    so this block's audio is still pure input + click.
    if (preRollActive.load (std::memory_order_acquire))
    {
        const int64_t startPos = transportPosition.load (std::memory_order_acquire);
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
            beginActualRecording();
        }
        else
        {
            transportPosition.store (newPos, std::memory_order_release);
        }
    }

    // 5. Click during recording. Added after the record write so the click
    //    is in the output but never in the recording.
    if (recordingRequested.load (std::memory_order_acquire)
        && metronomeEnabled.load (std::memory_order_acquire))
    {
        const int64_t startPos = transportPosition.load (std::memory_order_acquire);
        renderMetronomeInBlock (buffer, startPos, numSamples);
        transportPosition.store (startPos + numSamples, std::memory_order_release);
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
    if (clickBuffer.empty() || numSamples <= 0)
        return;

    const double bpmValue = bpm.load (std::memory_order_acquire);
    if (bpmValue <= 0.0 || currentSampleRate <= 0.0)
        return;

    const double samplesPerBeat = 60.0 / bpmValue * currentSampleRate;
    if (samplesPerBeat <= 0.0)
        return;

    const int numChannels = buffer.getNumChannels();
    const int clickLen    = static_cast<int> (clickBuffer.size());

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

        const int remaining = juce::jmin (clickLen, numSamples - blockOffset);
        for (int j = 0; j < remaining; ++j)
        {
            const float sample = clickBuffer[static_cast<size_t> (j)];
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addSample (ch, blockOffset + j, sample);
        }
    }
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

void BluePrinterAudioProcessor::computeLevels (const juce::AudioBuffer<float>& source, int numSamples)
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

    const float prevLevel = inputLevel.load (std::memory_order_acquire);
    const float prevPeak  = inputPeak.load  (std::memory_order_acquire);
    const float alpha     = 1.0f / static_cast<float> (levelSmoothing);
    const float newLevel  = prevLevel + (static_cast<float> (rms) - prevLevel) * alpha;
    const float decayPeak = prevPeak * 0.95f;

    inputLevel.store (newLevel, std::memory_order_release);
    inputPeak.store  (juce::jmax (peak, decayPeak), std::memory_order_release);
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

    const int beats = countInBeats.load (std::memory_order_acquire);
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
}

void BluePrinterAudioProcessor::stopRecording()
{
    // Cancel count-in if one is running. Nothing was recorded.
    if (preRollActive.load (std::memory_order_acquire))
    {
        preRollActive.store (false, std::memory_order_release);
        transportPosition.store (0, std::memory_order_release);
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
        library.persistMetadata (id);
        listeners.call ([](Listener& l) { l.libraryChanged(); });
    }
    return ok;
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
            library.persistMetadata (id);
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

juce::String BluePrinterAudioProcessor::getLastSaveError() const
{
    juce::ScopedLock lock (libraryFolderLock);
    return lastSaveError;
}

juce::String BluePrinterAudioProcessor::getLastChainRestoreError() const
{
    return lastChainRestoreError;
}

void BluePrinterAudioProcessor::clearLastChainRestoreError()
{
    lastChainRestoreError.clear();
}

//==============================================================================
void BluePrinterAudioProcessor::timerCallback()
{
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
    // VST3 chain: per-slot path + bypass + base64 plugin state, stored
    // as a JSON string so ValueTree can carry an arbitrary blob.
    state.setProperty ("pluginChain", juce::JSON::toString (pluginChain.getChainState(), true), nullptr);
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

            const auto chainJson = state.getProperty ("pluginChain").toString();
            if (chainJson.isNotEmpty())
            {
                const auto chainVar = juce::JSON::parse (chainJson);
                juce::String error;
                pluginChain.setChainState (chainVar, error);
                if (error.isNotEmpty())
                {
                    // Stash for the UI to display when it opens.
                    lastChainRestoreError = error;
                }
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

    // 2. VST3 chain. Guarded so the addPlugin calls inside don't
    // trigger a redundant write back to the file.
    const auto chainJson = props->getValue ("pluginChain");
    if (chainJson.isNotEmpty())
    {
        const auto chainVar = juce::JSON::parse (chainJson);
        if (chainVar.isObject())
        {
            persistingPluginChain = true;
            juce::String error;
            pluginChain.setChainState (chainVar, error);
            persistingPluginChain = false;
            if (error.isNotEmpty())
                lastChainRestoreError = error;
        }
    }
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
    if (auto* props = getUserState())
    {
        props->setValue ("pluginChain",
                         juce::JSON::toString (pluginChain.getChainState(), false));
        props->saveIfNeeded();
    }
}
