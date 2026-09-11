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

    // Tempo context for the next renders. Call once per block before render.
    void setContext (double newSampleRate, double newBpm, int newBeatsPerBar);

    // Adds the beats in [startPos, startPos + numSamples) into `buffer`.
    // `tick` / `accent` are the pre-rendered click waveforms (either may be
    // null). A backward jump in startPos is a clock reset and drops any
    // ringing clicks.
    void render (juce::AudioBuffer<float>& buffer,
                 juce::int64 startPos,
                 int numSamples,
                 const std::shared_ptr<const std::vector<float>>& tick,
                 const std::shared_ptr<const std::vector<float>>& accent);

    // Drops any ringing clicks (e.g. on release).
    void reset();

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
};
