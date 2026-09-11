#pragma once

#include <JuceHeader.h>
#include <vector>

// Metronome click synthesis (the audible click used by the metronome, the
// count-in and the free-running MIDI clock).
//
// A click is a short, exponentially-decaying stack of harmonics plus a
// burst of onset noise. Rendering is deterministic (a fixed LCG seed), so
// the click sounds identical every launch. The audio thread mixes the
// returned mono buffer into its output; the caller owns it through
// shared_ptr<const std::vector<float>>.
//
// Pure DSP with no processor or device state, so it is unit-tested
// directly (see Tests/test_ClickSynth.cpp).
namespace ClickSynth
{
    // One click voice: either the soft beat tick or the louder bar accent.
    // All fields are user-domain; render() clamps them, so callers may pass
    // raw settings. `decayRate` deliberately allows values below the
    // user-facing 20 because the accent derives its rate as tick * 0.78.
    struct Voice
    {
        double fundamental = 1000.0;   // Hz; clamped to [400, 3000]
        double decayRate   = 90.0;     // envelope exponent; clamped to [0, 1000]
        double duration    = 0.040;    // seconds of output
        float  amplitude   = 0.35f;    // clamped to [0, 1]
        float  noise       = 0.10f;    // onset noise; clamped to [0, 0.3]

        // A copy with every field in range.
        Voice clamped() const;
    };

    // Renders one voice at `sampleRate`. Returns an empty vector when
    // sampleRate <= 0, otherwise sampleRate * duration samples.
    std::vector<float> render (double sampleRate, const Voice& voice);
}
