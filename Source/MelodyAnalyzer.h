#pragma once

#include <JuceHeader.h>
#include <vector>

// Offline monophonic melody extraction for a recorded take (ticket 0054).
//
// Frames the buffer and runs the proven YIN detector (Tuner::detectPitch) per
// overlapping window, gates unvoiced frames on confidence, median-filters the
// pitch track to drop octave/vibrato blips, then segments stable runs into
// note events. Key + pitch-class analysis is delegated to KeyDetector.
//
// Kept free of processor state so it can be unit-tested with synthetic tones
// (Tests/test_MelodyAnalyzer.cpp) and run off the message thread.
class MelodyAnalyzer
{
public:
    struct NoteEvent
    {
        int64_t startSample   = 0;
        int64_t lengthSamples = 0;
        int     midi          = 0;     // nearest MIDI note (69 = A4)
        float   cents         = 0.0f;  // mean deviation from `midi`, -50..50
        float   confidence    = 0.0f;  // mean voicing confidence
    };

    struct Result
    {
        std::vector<NoteEvent> notes;          // ordered by start sample
        juce::String           key;            // empty when no confident key
        float                  keyConfidence = 0.0f;
        juce::StringArray      detectedNotes;  // pitch classes, strongest first
    };

    // Tunable analysis constants, exposed so tests can pin behaviour.
    struct Settings
    {
        int   frameSize     = 2048;    // YIN window (samples)
        int   hopSize       = 512;     // frame step (samples)
        float minHz         = 70.0f;   // low end of the pitch search
        float maxHz         = 1100.0f; // high end (voice range)
        float minConfidence = 0.5f;    // YIN aperiodicity gate for a voiced frame
        int   medianWindow  = 5;       // frames, odd
        int   minNoteFrames = 4;       // shorter runs are dropped
        int   maxGapFrames  = 2;       // legato gaps merged
        bool  detectKey     = true;
    };

    static Result analyze (const juce::AudioBuffer<float>& audio,
                           double sampleRate,
                           const Settings& settings = {});
};
