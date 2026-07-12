/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebViewEditor.h"

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

    startTimerHz (transportTimerHz);
}

void BluePrinterAudioProcessor::releaseResources()
{
    stopTimer();
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

    // Recording: capture the post-gain signal into the pre-allocated record
    // buffer. All math here is allocation-free, and access to recordBuffer is
    // serialised with the message thread via recordLock.
    {
        const juce::ScopedLock sl (recordLock);
        if (recordingRequested.load (std::memory_order_acquire))
            writeRecording (buffer, numSamples);
    }

    computeLevels (buffer, numSamples);

    // Playback: overwrite the output with the stored snippet. We do this
    // after recording so any monitoring of the input stops while a snippet
    // is playing.
    if (playbackActive.load (std::memory_order_acquire))
        renderPlayback (buffer, numSamples);
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
    if (recordingRequested.load (std::memory_order_acquire))
        return;

    if (playbackActive.load (std::memory_order_acquire))
        stopPlayback();

    if (recordBuffer == nullptr || maxRecordSamples <= 0)
        return;

    {
        const juce::ScopedLock sl (recordLock);
        recordBuffer->clear();
        recordWritePos.store (0, std::memory_order_release);
        recordingState.store (RecordingState::Recording, std::memory_order_release);
        recordingFinalizePending.store (false, std::memory_order_release);
    }
    recordingRequested.store (true, std::memory_order_release);

    listeners.call ([](Listener& l) { l.transportChanged(); });
}

void BluePrinterAudioProcessor::stopRecording()
{
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
    const bool removed = library.removeSnippet (id);
    if (removed)
    {
        if (playingSnippetId.load() == id)
            stopPlayback();
        listeners.call ([](Listener& l) { l.libraryChanged(); l.transportChanged(); });
    }
    return removed;
}

bool BluePrinterAudioProcessor::updateSnippetMeta (int id, const juce::String& name, const juce::String& comments)
{
    const bool ok = library.updateMeta (id, name, comments);
    if (ok)
        listeners.call ([](Listener& l) { l.libraryChanged(); });
    return ok;
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
    listeners.call ([](Listener& l) { l.libraryChanged(); });
}

juce::String BluePrinterAudioProcessor::getLastSaveError() const
{
    juce::ScopedLock lock (libraryFolderLock);
    return lastSaveError;
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
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void BluePrinterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
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
