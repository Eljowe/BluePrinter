#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

#include "MelodyAnalyzer.h"

// Auditions an extracted melody (ticket 0054). A preallocated sine + ADSR
// renders the note events; the message thread swaps the note list in and
// starts/stops playback, the audio thread renders additively into the output.
// Monitor-only: the caller decides where in processBlock it is mixed, and no
// allocation or lock happens in render().
class MelodyPlayer
{
public:
    using Notes = std::vector<MelodyAnalyzer::NoteEvent>;

    void prepare (double newSampleRate);

    // Message thread. The note list is shared with the audio thread; replace
    // only while stopped (the processor stops playback first).
    void setNotes (Notes newNotes);
    void clearNotes();

    void start (int64_t fromSample = 0);
    void stop();
    bool isPlaying() const { return playing.load (std::memory_order_acquire); }
    int64_t getPosition() const { return position.load (std::memory_order_acquire); }
    int64_t getLength() const { return length.load (std::memory_order_acquire); }

    // Audio thread. Adds the active notes into `dest` and advances the
    // playhead. Returns true while still playing, false once it has ended (or
    // was never started). `dest` keeps whatever it already held.
    bool render (juce::AudioBuffer<float>& dest, int numSamples);

    static constexpr float  amplitude      = 0.2f;
    static constexpr double attackSeconds  = 0.01;
    static constexpr double releaseSeconds = 0.05;

private:
    std::shared_ptr<const Notes> notes;
    std::atomic<bool>    playing  { false };
    std::atomic<int64_t> position { 0 };
    std::atomic<int64_t> length   { 0 };
    double sampleRate = 44100.0;
};
