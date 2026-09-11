#include "LooperGridMath.h"

#include <cmath>

namespace LooperGrid
{
int64_t computeLength (int64_t capturedSamples, double sampleRate, float bpm, int64_t maxSamples)
{
    if (capturedSamples <= 0 || sampleRate <= 0.0)
        return 0;

    const auto beat = 60.0 * sampleRate / juce::jmax (1.0f, bpm);
    const auto bar  = beat * 4.0;

    auto target = static_cast<int64_t> (std::llround (static_cast<double> (capturedSamples) / bar) * bar);
    if (target <= 0)
        target = static_cast<int64_t> (std::llround (static_cast<double> (capturedSamples) / beat) * beat);

    return juce::jlimit<int64_t> (1, juce::jmax<int64_t> (1, maxSamples), target);
}

int64_t computeFixedLengthSamples (int bars, double sampleRate, float bpm)
{
    if (bars <= 0 || sampleRate <= 0.0)
        return 0;

    const auto beat = 60.0 * sampleRate / juce::jmax (1.0f, bpm);
    return static_cast<int64_t> (std::llround (static_cast<double> (bars) * 4.0 * beat));
}
}
