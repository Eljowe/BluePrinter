#include "TestRunner.h"
#include "DelayLine.h"
#include "DelayCompensation.h"

#include <initializer_list>

namespace
{
juce::AudioBuffer<float> bufferFrom (std::initializer_list<float> values, int channels = 1)
{
    juce::AudioBuffer<float> buffer (channels, static_cast<int> (values.size()));
    int i = 0;
    for (float v : values)
        buffer.setSample (0, i++, v);
    return buffer;
}
}

BP_TEST (DelayLine_zeroDelayIsABitIdenticalNoOp)
{
    DelayLine line;
    line.prepare (1, 16);
    line.setDelay (0);

    auto buffer = bufferFrom ({ 0.1f, -0.5f, 0.25f, 1.0f });
    line.process (buffer, 4);

    BP_CHECK_NEAR (buffer.getSample (0, 0), 0.1f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 1), -0.5f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 2), 0.25f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 3), 1.0f, 0.000001f);
}

BP_TEST (DelayLine_unpreparedIsANoOp)
{
    DelayLine line;
    BP_CHECK (! line.isPrepared());

    auto buffer = bufferFrom ({ 0.1f, 0.2f });
    line.setDelay (4);
    line.process (buffer, 2);

    BP_CHECK_NEAR (buffer.getSample (0, 0), 0.1f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 1), 0.2f, 0.000001f);
}

BP_TEST (DelayLine_delaysAnImpulseByN)
{
    DelayLine line;
    line.prepare (1, 8);
    line.setDelay (3);

    auto buffer = bufferFrom ({ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    line.process (buffer, 6);

    // Zeros before the first delayed sample, then the impulse.
    BP_CHECK_NEAR (buffer.getSample (0, 0), 0.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 1), 0.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 2), 0.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 3), 1.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 4), 0.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 5), 0.0f, 0.000001f);
}

BP_TEST (DelayLine_channelsAreIndependent)
{
    DelayLine line;
    line.prepare (2, 4);
    line.setDelay (2);

    juce::AudioBuffer<float> buffer (2, 4);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    for (int i = 0; i < 4; ++i)
        buffer.setSample (1, i, 5.0f);

    line.process (buffer, 4);

    BP_CHECK_NEAR (buffer.getSample (0, 0), 0.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 1), 0.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 2), 1.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 3), 0.0f, 0.000001f);

    // Channel 1 has its own history: the constant signal is delayed by 2
    // (zeros first, then 5).
    BP_CHECK_NEAR (buffer.getSample (1, 0), 0.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (1, 1), 0.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (1, 2), 5.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (1, 3), 5.0f, 0.000001f);
}

BP_TEST (DelayLine_changingTheDelayClearsTheHistory)
{
    DelayLine line;
    line.prepare (1, 8);
    line.setDelay (3);

    auto first = bufferFrom ({ 1.0f, 0.0f, 0.0f, 0.0f });
    line.process (first, 4);
    BP_CHECK_NEAR (first.getSample (0, 3), 1.0f, 0.000001f);

    // The change is a discontinuity: the pending impulse must not leak into
    // the new alignment.
    line.setDelay (1);
    auto second = bufferFrom ({ 0.0f, 0.0f, 0.0f });
    line.process (second, 3);

    BP_CHECK_NEAR (second.getSample (0, 0), 0.0f, 0.000001f);
    BP_CHECK_NEAR (second.getSample (0, 1), 0.0f, 0.000001f);
    BP_CHECK_NEAR (second.getSample (0, 2), 0.0f, 0.000001f);
}

BP_TEST (DelayLine_clampsTheDelayToThePreparedMaximum)
{
    DelayLine line;
    line.prepare (1, 8);
    line.setDelay (100);
    BP_CHECK_EQ (line.getDelay(), 8);

    auto buffer = bufferFrom ({ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    line.process (buffer, 10);
    BP_CHECK_NEAR (buffer.getSample (0, 8), 1.0f, 0.000001f);
    BP_CHECK_NEAR (buffer.getSample (0, 9), 0.0f, 0.000001f);
}

BP_TEST (DelayLine_delayCarriesAcrossBlockBoundaries)
{
    DelayLine line;
    line.prepare (1, 8);
    line.setDelay (2);

    auto first = bufferFrom ({ 1.0f, 0.0f, 0.0f });
    line.process (first, 3);
    BP_CHECK_NEAR (first.getSample (0, 0), 0.0f, 0.000001f);
    BP_CHECK_NEAR (first.getSample (0, 1), 0.0f, 0.000001f);
    BP_CHECK_NEAR (first.getSample (0, 2), 1.0f, 0.000001f);

    // The impulse has fully flushed; the next block is silent.
    auto second = bufferFrom ({ 0.0f, 0.0f, 0.0f });
    line.process (second, 3);
    BP_CHECK_NEAR (second.getSample (0, 0), 0.0f, 0.000001f);
    BP_CHECK_NEAR (second.getSample (0, 1), 0.0f, 0.000001f);
    BP_CHECK_NEAR (second.getSample (0, 2), 0.0f, 0.000001f);
}

BP_TEST (DelayLine_alignsTwoSourcesToTheSameInstant)
{
    // Dry latency 0, chain latency 3 -> L = 3, delays { 3, 0 }.
    const auto result = DelayCompensation::compute ({ 0, 3 }, 100);
    BP_CHECK_EQ (result.uniformLatency, 3);

    DelayLine dryLine, chainLine;
    dryLine.prepare (1, 8);
    chainLine.prepare (1, 8);
    dryLine.setDelay (result.delays[0]);
    chainLine.setDelay (result.delays[1]);

    // The chain's plugin already delayed its impulse by 3; the dry impulse is
    // at 0. After compensation both land at sample 3, so the sum is coherent
    // (2 at sample 3, silence elsewhere) instead of two combed impulses.
    auto dryBuffer = bufferFrom ({ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    auto chainBuffer = bufferFrom ({ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f });

    dryLine.process (dryBuffer, 6);
    chainLine.process (chainBuffer, 6);

    for (int i = 0; i < 6; ++i)
    {
        const float sum = dryBuffer.getSample (0, i) + chainBuffer.getSample (0, i);
        BP_CHECK_NEAR (sum, i == 3 ? 2.0f : 0.0f, 0.000001f);
    }
}
