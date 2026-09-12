#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <vector>

// Pure monophonic pitch detection for the built-in tuner (ticket 0034).
//
// Autocorrelation with parabolic interpolation, driven from the clean
// post-gain input. Kept header-only and free of processor state so it can be
// unit-tested with synthetic tones (Tests/test_Tuner.cpp) and called off the
// audio thread by the tuner worker.
namespace Tuner
{
    struct Reading
    {
        float frequency  = 0.0f;   // Hz; 0 when there is no confident pitch
        float confidence = 0.0f;   // 0..1 (normalised autocorrelation peak)
    };

    // Estimates the fundamental of a mono buffer. `minHz`/`maxHz` bound the
    // search to the instrument range (defaults cover guitar). Returns {0,0}
    // for silence, near-silence, or a signal with no clear period.
    inline Reading detectPitch (const float* samples, int numSamples, double sampleRate,
                                float minHz = 60.0f, float maxHz = 1500.0f)
    {
        Reading result;
        if (samples == nullptr || numSamples <= 0 || sampleRate <= 0.0
            || minHz <= 0.0f || maxHz <= minHz)
            return result;

        // Remove DC and bail out on near-silence (so decayed notes report
        // "—" rather than a stale pitch).
        double mean = 0.0;
        for (int i = 0; i < numSamples; ++i)
            mean += static_cast<double> (samples[i]);
        mean /= static_cast<double> (numSamples);

        double energy = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const double v = static_cast<double> (samples[i]) - mean;
            energy += v * v;
        }
        if (energy <= 0.0)
            return result;

        const double rms = std::sqrt (energy / static_cast<double> (numSamples));
        if (rms < 1.0e-4)
            return result;

        const int minLag = juce::jmax (2, static_cast<int> (sampleRate / static_cast<double> (maxHz)));
        const int maxLag = juce::jmin (numSamples - 2, static_cast<int> (sampleRate / static_cast<double> (minHz)));
        if (maxLag <= minLag)
            return result;

        // YIN: cumulative-mean-normalised difference. Plain autocorrelation's
        // lag-0 lobe defeats it on low notes (low E read as ~1523 Hz), but the
        // CMNDF normalisation cancels that.
        std::vector<double> diff (static_cast<size_t> (maxLag) + 1, 0.0);
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double sum = 0.0;
            const int count = numSamples - tau;
            for (int i = 0; i < count; ++i)
            {
                const double delta = static_cast<double> (samples[i])
                                   - static_cast<double> (samples[i + tau]);
                sum += delta * delta;
            }
            diff[static_cast<size_t> (tau)] = sum;
        }

        std::vector<double> cmndf (static_cast<size_t> (maxLag) + 1, 1.0);
        double running = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            running += diff[static_cast<size_t> (tau)];
            cmndf[static_cast<size_t> (tau)] = running > 0.0
                ? diff[static_cast<size_t> (tau)] * static_cast<double> (tau) / running
                : 1.0;
        }

        // First dip below the aperiodicity threshold, then its local minimum.
        const double threshold = 0.15;
        int tau = minLag;
        while (tau <= maxLag && cmndf[static_cast<size_t> (tau)] >= threshold)
            ++tau;
        if (tau > maxLag)
            return result;

        while (tau + 1 <= maxLag
               && cmndf[static_cast<size_t> (tau + 1)] < cmndf[static_cast<size_t> (tau)])
            ++tau;

        // Parabolic interpolation on the CMNDF dip for sub-sample accuracy.
        const double y0 = tau > 1 ? cmndf[static_cast<size_t> (tau - 1)] : cmndf[static_cast<size_t> (tau)];
        const double y1 = cmndf[static_cast<size_t> (tau)];
        const double y2 = tau < maxLag ? cmndf[static_cast<size_t> (tau + 1)] : cmndf[static_cast<size_t> (tau)];
        const double denom = y0 - 2.0 * y1 + y2;
        double refinedLag = static_cast<double> (tau);
        if (std::abs (denom) > 1.0e-12)
            refinedLag += 0.5 * (y0 - y2) / denom;
        if (refinedLag <= 0.0)
            return result;

        result.frequency  = static_cast<float> (sampleRate / refinedLag);
        result.confidence = static_cast<float> (juce::jlimit (0.0, 1.0, 1.0 - y1));
        return result;
    }

    // Maps a frequency to the nearest note name (scientific pitch, A4 = 440)
    // and the deviation in cents. Empty note / 0 cents when frequency is
    // non-positive.
    inline void describePitch (float frequency, float referenceHz,
                               juce::String& outNote, float& outCents)
    {
        static constexpr const char* names[] =
            { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        outNote = {};
        outCents = 0.0f;
        if (frequency <= 0.0f || referenceHz <= 0.0f)
            return;

        const double semitones = 12.0 * std::log2 (static_cast<double> (frequency)
                                                   / static_cast<double> (referenceHz));
        const int nearest = static_cast<int> (std::lround (semitones));
        outCents = static_cast<float> ((semitones - static_cast<double> (nearest)) * 100.0);

        const int midi = 69 + nearest;   // MIDI note 69 = A4
        const int pitchClass = ((midi % 12) + 12) % 12;
        outNote = juce::String (names[pitchClass]) + juce::String (midi / 12 - 1);
    }
}
