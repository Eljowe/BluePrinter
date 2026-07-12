/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SnippetLibrary.h"

//==============================================================================
/**
*/
class BluePrinterAudioProcessor  : public juce::AudioProcessor, private juce::Timer
{
public:
    //==============================================================================
    BluePrinterAudioProcessor();
    ~BluePrinterAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts {*this, nullptr, "Parameters", createParameterLayout()};

    //==============================================================================
    // Recording / playback / library

    enum class RecordingState : int
    {
        Idle = 0,
        Recording = 1
    };

    void startRecording();
    void stopRecording();
    bool isRecordingRequested() const { return recordingRequested.load (std::memory_order_acquire); }

    void startPlayback (int snippetId);
    void stopPlayback();
    bool isPlaybackActive() const { return playbackActive.load (std::memory_order_acquire); }

    bool deleteSnippet (int id);
    bool updateSnippetMeta (int id, const juce::String& name, const juce::String& comments);

    SnippetLibrary& getLibrary() { return library; }
    const SnippetLibrary& getLibrary() const { return library; }

    juce::String getLibraryFolder() const;
    void setLibraryFolder (const juce::File& folder);

    // Re-scans the current library folder for any new .wav files and adds
    // them to the in-memory library. Already-loaded files are skipped.
    void refreshLibraryFromFolder();

    juce::String getLastSaveError() const;

    // Meter values updated by the audio thread (peak + RMS over the last block).
    float getCurrentInputLevel() const { return inputLevel.load (std::memory_order_acquire); }
    float getCurrentInputPeak() const  { return inputPeak.load  (std::memory_order_acquire); }

    // Snippet currently being captured (only valid while recordingRequested is true).
    int getRecordingLengthSamples() const { return static_cast<int> (recordWritePos.load (std::memory_order_acquire)); }

    // Snippet currently being played back (-1 if none).
    int getPlayingSnippetId() const { return playingSnippetId.load (std::memory_order_acquire); }
    int getPlaybackPositionSamples() const { return static_cast<int> (playbackReadPos.load (std::memory_order_acquire)); }

    // Notification hook used by the editor so the UI can refresh on demand.
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void libraryChanged() {}
        virtual void transportChanged() {}
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    static constexpr int maxRecordingSeconds = 120;
    static constexpr int transportTimerHz    = 30;
    static constexpr int levelSmoothing      = 8;

private:
    void timerCallback();
    void finalizeRecordingOnMessageThread();

    void writeRecording (const juce::AudioBuffer<float>& source, int numSamples);
    void renderPlayback (juce::AudioBuffer<float>& destination, int numSamples);
    void computeLevels  (const juce::AudioBuffer<float>& source, int numSamples);

    juce::ListenerList<Listener> listeners;

    SnippetLibrary library;

    // Pre-allocated record buffer. Allocated on the message thread inside
    // prepareToPlay, written to from the audio thread — no allocations there.
    std::unique_ptr<juce::AudioBuffer<float>> recordBuffer;
    int maxRecordSamples = 0;
    juce::CriticalSection recordLock;

    std::atomic<RecordingState> recordingState { RecordingState::Idle };
    std::atomic<bool> recordingRequested { false };
    std::atomic<bool> recordingFinalizePending { false };
    std::atomic<int64_t> recordWritePos { 0 };

    std::atomic<bool> playbackActive { false };
    std::atomic<int> playingSnippetId { -1 };
    std::atomic<int64_t> playbackReadPos { 0 };

    // Cached audio-thread copies. Updated under the library lock briefly,
    // then held as shared_ptrs so playback can't dangle.
    std::shared_ptr<const Snippet> playbackSnippet;

    std::atomic<float> inputLevel { 0.0f };
    std::atomic<float> inputPeak  { 0.0f };

    juce::File libraryFolder;
    juce::CriticalSection libraryFolderLock;
    juce::String lastSaveError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BluePrinterAudioProcessor)
};
