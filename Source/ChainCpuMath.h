#pragma once

// Per-chain CPU cost math (0047). The processor times each chain's
// processBlock with a monotonic clock and normalises the elapsed time
// against the block budget (numSamples / sampleRate) to get a load
// fraction, then folds it into an exponential moving average. Pure — the
// clock reads and the atomics stay in the processor / PluginChain.
// Unit-tested directly (Tests/test_ChainCpuMath.cpp).
namespace ChainCpuMath
{
    // One-pole smoothing time constant, in audio blocks. At ~30 blocks/s a
    // 30-block constant is a ~1 s average: stable enough to read, short
    // enough to react when a chain is bypassed, emptied or swapped.
    inline constexpr float smoothingBlocks = 30.0f;

    // Elapsed time as a fraction of the block budget. Deliberately not
    // clamped: a chain that overruns its block should read above 1.0.
    // Returns 0 for a non-positive budget or negative elapsed time.
    inline float blockFraction (double elapsedSeconds, double blockSeconds)
    {
        if (blockSeconds <= 0.0 || elapsedSeconds < 0.0)
            return 0.0f;
        return static_cast<float> (elapsedSeconds / blockSeconds);
    }

    // One-pole smoothing toward `target`; `smoothing` is the time constant
    // in blocks. <= 0 snaps straight to the target.
    inline float smooth (float previous, float target, float smoothing)
    {
        if (smoothing <= 0.0f)
            return target;
        const float alpha = 1.0f / smoothing;
        return previous + (target - previous) * alpha;
    }

    inline float smooth (float previous, float target)
    {
        return smooth (previous, target, smoothingBlocks);
    }
} // namespace ChainCpuMath
