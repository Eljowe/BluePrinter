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

    // Fixed capture target (0022) in samples for `bars` bars (4 beats
    // each) at the given tempo/sample rate. Returns 0 for a non-positive
    // bar count or invalid sample rate. The caller clamps to its record
    // buffer capacity.
    int64_t computeFixedLengthSamples (int bars,
                                       double sampleRate,
                                       float bpm);

    // A whole-beat crop of a captured loop. `startBeats` is trimmed off the
    // front, `endBeats` off the back; the caller plays
    // [startSamples, startSamples + lengthSamples). Beats are measured
    // against the FULL loop (so moving a handle back restores the region it
    // cut off) and clamped so a start crop always leaves at least one beat,
    // and end can't cross the start.
    struct Crop
    {
        int     startBeats    = 0;
        int     endBeats      = 0;
        int64_t startSamples  = 0;
        int64_t lengthSamples = 0;
    };

    Crop computeCrop (int64_t fullSamples,
                      double sampleRate,
                      float bpm,
                      int startBeats,
                      int endBeats);
}
