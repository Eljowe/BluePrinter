#pragma once

#include <JuceHeader.h>
#include <cmath>

// MIDI clock scheduler math: 24 pulses per quarter note (PPQN), derived
// from the same continuous beat position the audible metronome uses so the
// clock and the click stay aligned.
//
// Pure — no device I/O. The processor adds the returned offsets as 0xF8
// events and mirrors them to the standalone's MIDI output. Kept in a header
// so the small template is unit-tested directly (Tests/test_MidiClockMath.cpp).
namespace MidiClock
{
    constexpr int pulsesPerQuarterNote = 24;

    // Samples between two clock pulses at `bpm`, or 0 when the tempo or
    // sample rate is not usable.
    inline double samplesPerPulse (double sampleRate, double bpm)
    {
        if (sampleRate <= 0.0 || bpm <= 0.0)
            return 0.0;
        return (60.0 * sampleRate) / (bpm * static_cast<double> (pulsesPerQuarterNote));
    }

    // Calls onPulse(offsetInBlock) for every clock pulse whose sample falls
    // in [metronomePos, metronomePos + numSamples), and returns how many
    // were emitted. The offset is always in [0, numSamples).
    template <typename OnPulse>
    int forEachPulse (juce::int64 metronomePos,
                      int numSamples,
                      double sampleRate,
                      double bpm,
                      OnPulse&& onPulse)
    {
        const double samplesPerClock = samplesPerPulse (sampleRate, bpm);
        if (samplesPerClock <= 0.0 || numSamples <= 0)
            return 0;

        const auto endPos = metronomePos + numSamples;
        const auto firstPulse = static_cast<juce::int64> (
            std::ceil (static_cast<double> (metronomePos) / samplesPerClock));
        const auto lastPulse = static_cast<juce::int64> (
            std::floor (static_cast<double> (endPos) / samplesPerClock));

        int emitted = 0;
        for (auto pulse = firstPulse; pulse <= lastPulse; ++pulse)
        {
            const auto pulseSample = static_cast<juce::int64> (pulse * samplesPerClock);
            const int offset = static_cast<int> (pulseSample - metronomePos);
            if (offset < 0 || offset >= numSamples)
                continue;

            onPulse (offset);
            ++emitted;
        }

        return emitted;
    }
}
