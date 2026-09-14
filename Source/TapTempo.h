#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

// Tap-tempo averaging (0049).
//
// Pure logic: feed it monotonically increasing tap timestamps (milliseconds)
// and it reports a whole-number BPM once enough taps have arrived. The most
// recent `maxIntervals` intervals are averaged (mean), so a player can settle
// into a steady tap. The history resets when the gap since the previous tap
// exceeds `resetGapMs`, or when the computed tempo leaves the supported range
// (so a stray slow/quick tap starts a fresh measurement rather than dragging
// the average to something unusable). No processor, device or JUCE state, so
// it is unit-tested directly (see Tests/test_TapTempo.cpp).
class TapTempo
{
public:
    // Mean of at most this many intervals (taps - 1).
    static constexpr int     maxIntervals = 5;
    // A gap longer than this begins a new measurement.
    static constexpr int64_t resetGapMs   = 2000;
    // Accepted tempo window; the UI BPM knob covers 40..240.
    static constexpr double  minBpm       = 40.0;
    static constexpr double  maxBpm       = 240.0;

    // Register a tap at `nowMs` (non-decreasing milliseconds). Returns the
    // averaged BPM as a whole number in [minBpm, maxBpm] once at least one
    // interval is known, otherwise nullopt.
    std::optional<int> tap (int64_t nowMs)
    {
        if (hasLastTap)
        {
            const int64_t gap = nowMs - lastTapMs;
            if (gap > resetGapMs)
                count = 0;                     // too slow to be the same tempo
            else if (gap > 0)
                pushInterval (gap);
        }

        lastTapMs  = nowMs;
        hasLastTap = true;

        if (count <= 0)
            return std::nullopt;

        int64_t sum = 0;
        for (int i = 0; i < count; ++i)
            sum += intervals[static_cast<size_t> (i)];

        const double mean = static_cast<double> (sum) / static_cast<double> (count);
        if (mean <= 0.0)
            return std::nullopt;

        // Round to a whole BPM first, then keep only values that land inside
        // the supported window; a tap (or pair) that averages outside it starts
        // a fresh measurement rather than reporting a nonsense tempo.
        const int rounded = static_cast<int> (std::lround (60000.0 / mean));
        if (rounded < static_cast<int> (minBpm) || rounded > static_cast<int> (maxBpm))
        {
            count = 0;
            return std::nullopt;
        }

        return rounded;
    }

    int getIntervalCount() const noexcept { return count; }

private:
    void pushInterval (int64_t dt) noexcept
    {
        if (count < maxIntervals)
        {
            intervals[static_cast<size_t> (count)] = dt;
            ++count;
            return;
        }

        // Full: drop the oldest interval and append the new one.
        for (int i = 1; i < maxIntervals; ++i)
            intervals[static_cast<size_t> (i - 1)] = intervals[static_cast<size_t> (i)];
        intervals[static_cast<size_t> (maxIntervals - 1)] = dt;
    }

    std::array<int64_t, maxIntervals> intervals {};
    int     count      = 0;
    int64_t lastTapMs  = 0;
    bool    hasLastTap = false;
};
