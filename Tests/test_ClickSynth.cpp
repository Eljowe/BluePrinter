#include "TestRunner.h"
#include "ClickSynth.h"

#include <cmath>
#include <cstdint>

namespace
{
// The exact algorithm ClickSynth was extracted from (PluginProcessor's
// makeClick lambda), kept here as the golden reference so the refactor is
// provably sample-for-sample equivalent.
std::vector<float> referenceRender (double sampleRate, double fundamental, double decayRate,
                                    double duration, float amplitude, float noiseLevel)
{
    const int n = juce::jmax (1, static_cast<int> (sampleRate * duration));
    std::vector<float> buf (static_cast<size_t> (n));

    const int attackSamples = juce::jmax (1, static_cast<int> (sampleRate * 0.002));
    const int fadeSamples   = juce::jmax (1, static_cast<int> (sampleRate * 0.002));
    const double noiseWindow = 0.004;

    uint32_t noiseState = 0x1B3F5A91u;
    auto nextNoise = [&noiseState]()
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return (static_cast<float> (noiseState) / static_cast<float> (0xFFFFFFFFu)) * 2.0f - 1.0f;
    };

    const float twoPi = juce::MathConstants<float>::twoPi;
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;

        double env = std::exp (-decayRate * t);
        if (i < attackSamples)
            env *= static_cast<double> (i) / attackSamples;
        const int tailLeft = n - i;
        if (tailLeft < fadeSamples)
            env *= static_cast<double> (tailLeft) / fadeSamples;

        const float f = static_cast<float> (t);
        const float tonal = std::sin (twoPi * static_cast<float> (fundamental) * f) * 0.55f
                          + std::sin (twoPi * static_cast<float> (fundamental * 2.0) * f) * 0.30f
                          + std::sin (twoPi * static_cast<float> (fundamental * 3.0) * f) * 0.15f;
        const float noise = t < noiseWindow ? nextNoise() * noiseLevel : 0.0f;

        buf[static_cast<size_t> (i)] = (tonal * amplitude + noise) * static_cast<float> (env);
    }
    return buf;
}
}

BP_TEST (ClickSynth_matchesTheOriginalAlgorithmExactly)
{
    // Frame values as the processor passes them (click pitch/decay are float).
    const float tickPitch = 1000.0f, accentPitch = 1500.0f;
    const float tickDecay = 90.0f, tickVol = 0.35f, accentVol = 0.5f, noise = 0.1f;

    for (const double sr : { 44100.0, 48000.0 })
    {
        const auto tick = ClickSynth::render (sr, { tickPitch, tickDecay, 0.040, tickVol, noise });
        const auto refTick = referenceRender (sr, tickPitch, tickDecay, 0.040, tickVol, noise);
        BP_CHECK (tick == refTick);

        const auto accent = ClickSynth::render (sr, { accentPitch, tickDecay * 0.78, 0.055, accentVol, noise });
        const auto refAccent = referenceRender (sr, accentPitch,
                                                static_cast<double> (tickDecay) * 0.78,
                                                0.055, accentVol, noise);
        BP_CHECK (accent == refAccent);
    }
}

BP_TEST (ClickSynth_rendersDeterministicallyAndClamps)
{
    ClickSynth::Voice tick;
    tick.fundamental = 1000.0;
    tick.decayRate   = 90.0;
    tick.duration    = 0.040;
    tick.amplitude   = 0.5f;
    tick.noise       = 0.0f;

    const auto first  = ClickSynth::render (48000.0, tick);
    const auto second = ClickSynth::render (48000.0, tick);

    // 40 ms at 48 kHz.
    BP_CHECK_EQ (static_cast<int> (first.size()), 1920);
    // Deterministic: same input, same samples (fixed LCG seed).
    BP_CHECK (first == second);

    float peak = 0.0f;
    for (auto s : first)
        peak = juce::jmax (peak, std::abs (s));
    BP_CHECK (peak > 0.01f);
    BP_CHECK (peak <= 1.0f);

    // A non-positive sample rate yields nothing.
    BP_CHECK (ClickSynth::render (0.0, tick).empty());

    // clamped() bounds raw user values but leaves the derived accent
    // decay (tick * 0.78) valid.
    ClickSynth::Voice raw;
    raw.fundamental = 5000.0;
    raw.decayRate   = 15.6;
    raw.amplitude   = 2.0f;
    raw.noise       = 1.0f;

    const auto clamped = raw.clamped();
    BP_CHECK_EQ (static_cast<int> (clamped.fundamental), 3000);
    BP_CHECK_NEAR (clamped.decayRate, 15.6, 0.001);
    BP_CHECK_NEAR (clamped.amplitude, 1.0f, 0.001f);
    BP_CHECK_NEAR (clamped.noise, 0.3f, 0.001f);
}
