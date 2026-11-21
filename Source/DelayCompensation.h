#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

// Plugin delay compensation maths (0046). Pure and header-only, verified by
// Tests/test_DelayCompensation.cpp.
//
// Every source (the dry pass-through plus each parallel chain) reports a
// latency in samples. One uniform latency L = max(source latencies) is used:
// each source is then delayed by `L - its own latency`, so all sources line up
// at the same point in time. The delay is capped so a plugin that reports a
// huge latency cannot force an unbounded delay line (the source beyond the cap
// is clamped and the caller notifies once).
namespace DelayCompensation
{
    struct Result
    {
        // The uniform latency all sources are aligned to: the maximum source
        // latency, clamped to [0, capSamples].
        int uniformLatency = 0;
        // Per source: max(0, uniformLatency - max(0, lat_i)). Same size as
        // the input vector (empty for no sources).
        std::vector<int> delays;
        // True when any source (and therefore the maximum) exceeded the cap.
        bool clamped = false;
    };

    // Fills `delaysOut` instead of allocating a result vector, so the audio
    // thread can call this with a pre-sized vector (a same-size resize is a
    // no-op). Returns the uniform latency.
    inline int computeInto (const std::vector<int>& latencies,
                            int capSamples,
                            std::vector<int>& delaysOut,
                            bool& clampedOut)
    {
        const int cap = std::max (0, capSamples);

        int maxLatency = 0;
        bool clamped = false;
        for (const int latency : latencies)
        {
            const int value = std::max (0, latency);
            if (value > cap)
                clamped = true;
            maxLatency = std::max (maxLatency, value);
        }

        const int uniform = std::min (maxLatency, cap);

        delaysOut.resize (latencies.size());
        for (std::size_t i = 0; i < latencies.size(); ++i)
            delaysOut[i] = std::max (0, uniform - std::max (0, latencies[i]));

        clampedOut = clamped;
        return uniform;
    }

    inline Result compute (const std::vector<int>& latencies, int capSamples)
    {
        Result result;
        result.delays.resize (latencies.size());
        result.uniformLatency = computeInto (latencies, capSamples,
                                             result.delays, result.clamped);
        return result;
    }
} // namespace DelayCompensation
