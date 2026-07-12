/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SnippetLibrary.h"
#include "PluginChain.h"
#include "KeyDetector.h"

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

    // Run musical-key detection on the snippet's audio. The FFT-based
    // chroma analysis runs on a worker thread; the snippet is updated
    // and the sidecar JSON rewritten on the message thread, then
    // listeners are notified. Replaces any previously detected key.
    void detectSnippetKeyAndNotes (int id);

    SnippetLibrary& getLibrary() { return library; }
    const SnippetLibrary& getLibrary() const { return library; }

    juce::String getLibraryFolder() const;
    void setLibraryFolder (const juce::File& folder);

    // Re-scans the current library folder for any new .wav files and adds
    // them to the in-memory library. Already-loaded files are skipped.
    void refreshLibraryFromFolder();

    // VST3 FX chain. The chain runs between the input and the recording
    // tap, so the recording captures the processed signal.
    PluginChain&       getPluginChain()       { return pluginChain; }
    const PluginChain& getPluginChain() const { return pluginChain; }

    // Metronome / count-in settings. Persisted in plugin state.
    bool    getMetronomeEnabled() const { return metronomeEnabled.load (std::memory_order_acquire); }
    float   getBpm()              const { return bpm.load (std::memory_order_acquire); }
    int     getCountInBeats()     const { return countInBeats.load (std::memory_order_acquire); }
    int64_t getTransportPosition() const { return transportPosition.load (std::memory_order_acquire); }
    bool    isPreRollActive()     const { return preRollActive.load (std::memory_order_acquire); }

    void setMetronomeEnabled (bool enabled);
    void setBpm (float newBpm);
    void setCountInBeats (int beats);

    juce::String getLastSaveError() const;

    // Set by setStateInformation when the saved VST3 chain couldn't be
    // fully restored (e.g. a plugin's license expired). Read by the UI
    // so the user knows what was skipped. Cleared explicitly.
    juce::String getLastChainRestoreError() const;
    void clearLastChainRestoreError();

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
        virtual void pluginChainChanged() {}
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    static constexpr int maxRecordingSeconds = 120;
    static constexpr int transportTimerHz    = 30;
    static constexpr int levelSmoothing      = 8;

private:
    void timerCallback();
    void finalizeRecordingOnMessageThread();
    void beginActualRecording();

    void writeRecording (const juce::AudioBuffer<float>& source, int numSamples);
    void renderPlayback (juce::AudioBuffer<float>& destination, int numSamples);
    void computeLevels  (const juce::AudioBuffer<float>& source, int numSamples);
    void renderMetronomeInBlock (juce::AudioBuffer<float>& buffer, int64_t startPos, int numSamples);

    juce::ListenerList<Listener> listeners;

    SnippetLibrary library;
    PluginChain    pluginChain;

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

    // Metronome / count-in. Settings are user-tweakable and persisted; the
    // preRoll* / transportPosition fields are audio-thread runtime state.
    std::atomic<bool>    metronomeEnabled { true };
    std::atomic<float>   bpm              { 120.0f };
    std::atomic<int>     countInBeats     { 4 };
    std::atomic<bool>    preRollActive    { false };
    std::atomic<int64_t> transportPosition { 0 };

    // Pre-rendered click sample (50 ms of decaying harmonics). Filled in
    // prepareToPlay, read-only on the audio thread.
    std::vector<float> clickBuffer;
    double currentSampleRate = 44100.0;

    // Cached audio-thread copies. Updated under the library lock briefly,
    // then held as shared_ptrs so playback can't dangle.
    std::shared_ptr<const Snippet> playbackSnippet;

    std::atomic<float> inputLevel { 0.0f };
    std::atomic<float> inputPeak  { 0.0f };

    juce::File libraryFolder;
    juce::CriticalSection libraryFolderLock;
    juce::String lastSaveError;

    // User-state persistence for the standalone build. Standalone
    // never has a host calling getStateInformation/setStateInformation
    // automatically, so without this the library folder and VST3
    // chain are lost on every restart. The file lives under
    // userApplicationDataDirectory/Retrokielto/BluePrinter.properties
    // (%APPDATA%/Retrokielto/BluePrinter.properties on Windows).
    std::unique_ptr<juce::PropertiesFile> userState;

    // Lazily open the properties file. Returns nullptr on disk failure
    // (read-only volume, missing perms) so the rest of the code can
    // keep working in-memory without crashing.
    juce::PropertiesFile* getUserState();

    // One-shot restore from userState: sets the library folder (which
    // also auto-loads snippets) and replays the saved VST3 chain.
    // Safe to call on every construction; no-op if userState is empty.
    void restoreUserState();

    // Write the library folder string to userState. Called from
    // setLibraryFolder.
    void persistLibraryFolder();

    // Serialise the VST3 chain to userState. Wired into
    // pluginChain.onChanged so it runs after every add/remove/
    // bypass/move. Skipped while a restore is in progress to avoid
    // writing the just-loaded state back over the file.
    void persistPluginChain();
    bool persistingPluginChain = false;

    // Stashed when setStateInformation fails to restore one or more
    // chain plugins (e.g. expired-license VST3s). Read by the UI on
    // open so the user knows what was skipped.
    juce::String lastChainRestoreError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BluePrinterAudioProcessor)
};
