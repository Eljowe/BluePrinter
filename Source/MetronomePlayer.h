#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

// Renders the metronome click into an audio block.
//
// Owns only the ringing state: a click burst outlives one block, so each
// scheduled click is kept and its tail rendered in later blocks (truncating
// at the block boundary made beats near block ends sound short/quiet, the
// long accents most). The click waveforms themselves are synthesised by
// ClickSynth and passed in per block by the processor, which preserves the
// existing message-thread resynth / audio-thread read hand-off.
//
// Audio-thread only. setContext/render/reset do not allocate (the ringing
// list is reserved in the constructor).
class MetronomePlayer
{
public:
    MetronomePlayer();

    // Tempo + meter context for the next renders. Call once per block before
    // render. BPM is a quarter-note tempo; `beatUnit` is the denominator, so
    // an eighth beat (beatUnit 8) is half a quarter. `subdivision` is the
    // number of clicks per beat (0/1 = beats only, 2 = eighths, 3 = triplets,
    // 4 = sixteenths); `accentMask` selects which beats of the bar carry the
    // accent voice, bit (beat index mod beatsPerBar).
    void setContext (double newSampleRate, double newBpm, int newBeatsPerBar, int newBeatUnit = 4,
                     int newSubdivision = 0, uint16_t newAccentMask = 0x0001);

    // Adds the clicks in [startPos, startPos + numSamples) into `buffer`.
    // `tick` / `accent` / `sub` are the pre-rendered click waveforms (any may
    // be null). Accented beats use `accent` per the context's accent mask;
    // other beats use `tick`; subdivision clicks use `sub` and are never
    // accented. A backward jump in startPos is a clock reset and drops any
    // ringing clicks.
    void render (juce::AudioBuffer<float>& buffer,
                 juce::int64 startPos,
                 int numSamples,
                 const std::shared_ptr<const std::vector<float>>& tick,
                 const std::shared_ptr<const std::vector<float>>& accent,
                 const std::shared_ptr<const std::vector<float>>& sub = nullptr);

    // Drops any ringing clicks (e.g. on release).
    void reset();

    // Click-rhythm helpers (0050). `normaliseSubdivision` accepts 2/3/4 and
    // turns anything else into 0 (= beats only). `fitAccentMaskToBeats` keeps
    // the low `beats` bits and clears the rest, so a meter change keeps the
    // pattern that fits and pads the new beats as unaccented.
    static int      normaliseSubdivision (int subdivision) noexcept;
    static uint16_t fitAccentMaskToBeats (uint16_t mask, int beats) noexcept;

private:
    struct ActiveClick
    {
        std::shared_ptr<const std::vector<float>> buffer;
        juce::int64 nextSample = 0;   // absolute sample buffer[readPos] lands on
        int         readPos    = 0;
    };

    void renderTail (juce::AudioBuffer<float>& buffer, ActiveClick& ac,
                     juce::int64 startPos, juce::int64 endPos, int numChannels);

    std::vector<ActiveClick> activeClicks;
    juce::int64 lastStartPos = 0;

    double sampleRate  = 0.0;
    double bpm         = 120.0;
    int    beatsPerBar = 4;
    int    beatUnit    = 4;
    int    subdivision = 0;
    uint16_t accentMask = 0x0001;
};
