#pragma once

#include <JuceHeader.h>
#include <cstdint>

// Pure looper grid-quantization math, extracted from
// BluePrinterAudioProcessor::trimLooperToMusicalGrid so it can be unit
// tested without an audio device. Stateless and free of processor state.
namespace LooperGrid
{
    // Whole-bar-snapped length (in samples) for a raw capture.
    //
    // The loop length is rounded to the nearest whole bar at the given
    // tempo (4 beats per bar); a capture shorter than half a bar falls
    // back to whole beats so it still lands on the grid; the result is
    // clamped to [1, maxSamples]. Returns 0 when the capture or sample
    // rate is non-positive (nothing to trim). A short capture is expected
    // to be zero-padded by the caller, and an overlong one truncated.
    int64_t computeLength (int64_t capturedSamples,
                           double sampleRate,
                           float bpm,
                           int64_t maxSamples);
}
