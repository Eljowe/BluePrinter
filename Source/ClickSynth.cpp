#include "ClickSynth.h"

#include <cmath>
#include <cstdint>

namespace ClickSynth
{

Voice Voice::clamped() const
{
    Voice v;
    v.fundamental = juce::jlimit (400.0, 3000.0, fundamental);
    // The accent rate is derived as tick * 0.78 (down to ~15.6), so the
    // lower bound is intentionally below the user-facing 20.
    v.decayRate   = juce::jlimit (0.0, 1000.0, decayRate);
    v.duration    = juce::jmax (0.001, duration);
    v.amplitude   = juce::jlimit (0.0f, 1.0f, amplitude);
    v.noise       = juce::jlimit (0.0f, 0.3f, noise);
    return v;
}

std::vector<float> render (double sampleRate, const Voice& rawVoice)
{
    std::vector<float> buffer;

    if (sampleRate <= 0.0)
        return buffer;

    const auto voice = rawVoice.clamped();

    const int n = juce::jmax (1, static_cast<int> (sampleRate * static_cast<double> (voice.duration)));
    buffer.resize (static_cast<size_t> (n));

    const int attackSamples = juce::jmax (1, static_cast<int> (sampleRate * 0.002));
    const int fadeSamples   = juce::jmax (1, static_cast<int> (sampleRate * 0.002));
    const double noiseWindow = 0.004;

    // Deterministic LCG so the click is identical every launch.
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

        double env = std::exp (-voice.decayRate * t);
        if (i < attackSamples)
            env *= static_cast<double> (i) / attackSamples;
        const int tailLeft = n - i;
        if (tailLeft < fadeSamples)
            env *= static_cast<double> (tailLeft) / fadeSamples;

        const float f = static_cast<float> (t);
        const float tonal = std::sin (twoPi * static_cast<float> (voice.fundamental) * f) * 0.55f
                          + std::sin (twoPi * static_cast<float> (voice.fundamental * 2.0) * f) * 0.30f
                          + std::sin (twoPi * static_cast<float> (voice.fundamental * 3.0) * f) * 0.15f;
        const float noise = t < noiseWindow ? nextNoise() * voice.noise : 0.0f;

        buffer[static_cast<size_t> (i)] = (tonal * voice.amplitude + noise) * static_cast<float> (env);
    }

    return buffer;
}

} // namespace ClickSynth
