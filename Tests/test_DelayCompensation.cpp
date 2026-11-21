#include "TestRunner.h"
#include "DelayCompensation.h"

BP_TEST (DelayCompensation_emptyHasNoLatency)
{
    const auto result = DelayCompensation::compute ({}, 1000);
    BP_CHECK_EQ (result.uniformLatency, 0);
    BP_CHECK (result.delays.empty());
    BP_CHECK (! result.clamped);
}

BP_TEST (DelayCompensation_zeroLatenciesNeedNoDelay)
{
    const auto result = DelayCompensation::compute ({ 0, 0, 0 }, 1000);
    BP_CHECK_EQ (result.uniformLatency, 0);
    BP_CHECK_EQ (result.delays.size(), static_cast<std::size_t> (3));
    for (const int delay : result.delays)
        BP_CHECK_EQ (delay, 0);
    BP_CHECK (! result.clamped);
}

BP_TEST (DelayCompensation_mixedSourcesAlignToTheMaximum)
{
    const auto result = DelayCompensation::compute ({ 0, 100, 40 }, 1000);
    BP_CHECK_EQ (result.uniformLatency, 100);
    BP_CHECK_EQ (result.delays.size(), static_cast<std::size_t> (3));
    BP_CHECK_EQ (result.delays[0], 100); // dry (0) waits the full L
    BP_CHECK_EQ (result.delays[1], 0);   // slowest waits nothing
    BP_CHECK_EQ (result.delays[2], 60);  // 100 - 40
    BP_CHECK (! result.clamped);
}

BP_TEST (DelayCompensation_negativeLatencyCountsAsZero)
{
    const auto result = DelayCompensation::compute ({ -20, 50 }, 1000);
    BP_CHECK_EQ (result.uniformLatency, 50);
    BP_CHECK_EQ (result.delays[0], 50);
    BP_CHECK_EQ (result.delays[1], 0);
    BP_CHECK (! result.clamped);
}

BP_TEST (DelayCompensation_clampsTheUniformLatencyToTheCap)
{
    const auto result = DelayCompensation::compute ({ 0, 4000 }, 1000);
    BP_CHECK_EQ (result.uniformLatency, 1000);
    BP_CHECK_EQ (result.delays[0], 1000);
    BP_CHECK_EQ (result.delays[1], 0); // cannot be pulled back further
    BP_CHECK (result.clamped);
}

BP_TEST (DelayCompensation_capBoundaryIsNotClamped)
{
    const auto result = DelayCompensation::compute ({ 500, 1000 }, 1000);
    BP_CHECK_EQ (result.uniformLatency, 1000);
    BP_CHECK_EQ (result.delays[0], 500);
    BP_CHECK (! result.clamped);
}

BP_TEST (DelayCompensation_zeroCapClampsEverything)
{
    const auto result = DelayCompensation::compute ({ 0, 10 }, 0);
    BP_CHECK_EQ (result.uniformLatency, 0);
    BP_CHECK_EQ (result.delays[0], 0);
    BP_CHECK_EQ (result.delays[1], 0);
    BP_CHECK (result.clamped);
}

BP_TEST (DelayCompensation_computeIntoReusesTheOutputVector)
{
    std::vector<int> delays { 9, 9, 9 };
    bool clamped = true;
    const auto uniform = DelayCompensation::computeInto ({ 10, 0 }, 100, delays, clamped);

    BP_CHECK_EQ (uniform, 10);
    BP_CHECK_EQ (delays.size(), static_cast<std::size_t> (2));
    BP_CHECK_EQ (delays[0], 0);
    BP_CHECK_EQ (delays[1], 10);
    BP_CHECK (! clamped);
}
