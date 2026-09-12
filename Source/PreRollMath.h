#pragma once

#include <JuceHeader.h>
#include <cstdint>

// The count-in completion test shared by the take recorder and the looper
// pre-roll: whether the continuous beat position has reached the configured
// number of beats. Pure (Tests/test_PreRollMath.cpp).
namespace PreRoll
{
    // True once `position` samples of the beat clock have elapsed `beats`
    // beats at the given tempo / sample rate. A non-positive beat count,
    // tempo or rate means there is nothing to count (complete immediately).
    inline bool isComplete (int64_t position, double sampleRate, float bpm, int beats)
    {
        if (beats <= 0 || sampleRate <= 0.0 || bpm <= 0.0)
            return true;

        const double samplesPerBeat = 60.0 / static_cast<double> (bpm) * sampleRate;
        return static_cast<int> (static_cast<double> (position) / samplesPerBeat) >= beats;
    }
}
