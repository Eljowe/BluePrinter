#include "TestRunner.h"
#include "ClickSynth.h"

#include <cmath>

BP_TEST (ClickSynth_rendersDeterministicallyAndClamps)
{
    ClickSynth::Voice tick;
    tick.fundamental = 1000.0f;
    tick.decayRate   = 90.0f;
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
    raw.fundamental = 5000.0f;
    raw.decayRate   = 15.6f;
    raw.amplitude   = 2.0f;
    raw.noise       = 1.0f;

    const auto clamped = raw.clamped();
    BP_CHECK_EQ (static_cast<int> (clamped.fundamental), 3000);
    BP_CHECK_NEAR (clamped.decayRate, 15.6f, 0.001f);
    BP_CHECK_NEAR (clamped.amplitude, 1.0f, 0.001f);
    BP_CHECK_NEAR (clamped.noise, 0.3f, 0.001f);
}
