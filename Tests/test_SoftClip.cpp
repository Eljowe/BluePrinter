#include "TestRunner.h"
#include "SoftClip.h"

#include <cmath>

// Monitor/output soft-clip guard (0048): fixed tanh saturation bounded by the
// ceiling (see Source/SoftClip.h).

namespace
{
juce::AudioBuffer<float> makeBuffer (std::initializer_list<float> left,
                                     std::initializer_list<float> right)
{
    juce::AudioBuffer<float> buffer (2, static_cast<int> (left.size()));
    int i = 0;
    for (auto v : left)
        buffer.setSample (0, i++, v);
    i = 0;
    for (auto v : right)
        buffer.setSample (1, i++, v);
    return buffer;
}
} // namespace

BP_TEST (SoftClip_ceilingGainConvertsAndIsUnityAtZero)
{
    BP_CHECK_NEAR (SoftClip::ceilingGain (-1.0f), 0.891, 0.0005);
    BP_CHECK_NEAR (SoftClip::ceilingGain (-6.0f), 0.501187, 0.0005);
    BP_CHECK_NEAR (SoftClip::ceilingGain (-12.0f), 0.251189, 0.0005);
    BP_CHECK_EQ (SoftClip::ceilingGain (0.0f), 1.0f);
}

BP_TEST (SoftClip_outputNeverExceedsTheCeiling)
{
    // Inputs far above the ceiling must still land at or below it, for every
    // ceiling in the supported -12..0 dBFS range (0.5 dB step).
    for (float ceilingDb = -12.0f; ceilingDb <= 0.0f; ceilingDb += 0.5f)
    {
        const float ceiling = SoftClip::ceilingGain (ceilingDb);

        for (const float x : { 0.5f, 1.0f, 2.0f, 5.0f, 20.0f, 1000.0f })
        {
            const float y = SoftClip::shape (x, ceiling);
            const float yn = SoftClip::shape (-x, ceiling);

            BP_CHECK (std::abs (y)  <= ceiling);
            BP_CHECK (std::abs (yn) <= ceiling);
            // Odd-symmetric at the same ceiling.
            BP_CHECK_NEAR (yn, -y, 1.0e-6);
        }
    }
}

BP_TEST (SoftClip_nearUnityWellBelowTheCeiling)
{
    for (const float ceilingDb : { -12.0f, -6.0f, -1.0f, 0.0f })
    {
        const float ceiling = SoftClip::ceilingGain (ceilingDb);

        for (const float fraction : { 0.05f, 0.1f })
        {
            const float x = fraction * ceiling;
            const float y = SoftClip::shape (x, ceiling);

            // Within ~1% of the input that far below the knee.
            BP_CHECK_NEAR (y, x, 0.01 * std::abs (x));
        }
    }
}

BP_TEST (SoftClip_monotonicAcrossSweep)
{
    const float ceiling = SoftClip::ceilingGain (-1.0f);
    const float limit = 2.0f * ceiling;
    const int steps = 2000;

    float previous = SoftClip::shape (-limit, ceiling);

    for (int i = 1; i <= steps; ++i)
    {
        const float x = -limit + (2.0f * limit) * (static_cast<float> (i) / static_cast<float> (steps));
        const float y = SoftClip::shape (x, ceiling);

        BP_CHECK (y >= previous); // non-decreasing
        previous = y;
    }

    // Strictly increasing across the knee.
    BP_CHECK (SoftClip::shape (0.5f * ceiling, ceiling)
              < SoftClip::shape (0.6f * ceiling, ceiling));
}

BP_TEST (SoftClip_oddSymmetric)
{
    const float ceiling = SoftClip::ceilingGain (-3.0f);

    for (const float x : { 0.0f, 0.05f, 0.2f, 0.5f, 1.0f, 4.0f })
        BP_CHECK_NEAR (SoftClip::shape (-x, ceiling), -SoftClip::shape (x, ceiling), 1.0e-6);
}

BP_TEST (SoftClip_deterministic)
{
    const float ceiling = SoftClip::ceilingGain (-2.0f);

    for (const float x : { 0.01f, 0.3f, 0.9f, 3.0f })
    {
        const float first = SoftClip::shape (x, ceiling);
        const float second = SoftClip::shape (x, ceiling);

        BP_CHECK_EQ (first, second);
    }
}

BP_TEST (SoftClip_processBufferDisabledIsNoOp)
{
    auto buffer = makeBuffer ({ 2.0f, -3.0f, 0.5f }, { -1.5f, 0.25f, 4.0f });
    const auto before = buffer;

    const bool engaged = SoftClip::processBuffer (buffer, 3, SoftClip::ceilingGain (-1.0f), false);

    BP_CHECK (! engaged);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 3; ++i)
            BP_CHECK_EQ (buffer.getSample (channel, i), before.getSample (channel, i));
}

BP_TEST (SoftClip_processBufferNonPositiveCeilingIsNoOp)
{
    auto buffer = makeBuffer ({ 2.0f, -3.0f, 0.5f }, { -1.5f, 0.25f, 4.0f });
    const auto before = buffer;

    BP_CHECK (! SoftClip::processBuffer (buffer, 3, 0.0f, true));
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 3; ++i)
            BP_CHECK_EQ (buffer.getSample (channel, i), before.getSample (channel, i));
}

BP_TEST (SoftClip_processBufferLatchAndBounds)
{
    const float ceiling = SoftClip::ceilingGain (-1.0f);

    // All samples below the ceiling: no engage, still guard-shaped (a no-op
    // at this magnitude to within float noise).
    {
        auto buffer = makeBuffer ({ 0.1f, -0.2f, 0.0f }, { 0.05f, -0.09f, 0.2f });
        const bool engaged = SoftClip::processBuffer (buffer, 3, ceiling, true);

        BP_CHECK (! engaged);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 3; ++i)
                BP_CHECK (std::abs (buffer.getSample (channel, i)) <= ceiling);
    }

    // One hot sample engages the latch; every output stays within the ceiling.
    {
        auto buffer = makeBuffer ({ 0.1f, -2.0f, 0.3f }, { 3.0f, 0.2f, -0.4f });
        const bool engaged = SoftClip::processBuffer (buffer, 3, ceiling, true);

        BP_CHECK (engaged);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 3; ++i)
                BP_CHECK (std::abs (buffer.getSample (channel, i)) <= ceiling);
    }

    // A sample exactly at the ceiling does not engage (nothing exceeded).
    {
        auto buffer = makeBuffer ({ ceiling, -ceiling, 0.0f }, { 0.0f, 0.0f, 0.0f });
        BP_CHECK (! SoftClip::processBuffer (buffer, 3, ceiling, true));
    }

    // Every supported ceiling bounds the buffer, and a full-scale input engages.
    for (float ceilingDb = -12.0f; ceilingDb <= 0.0f; ceilingDb += 0.5f)
    {
        const float c = SoftClip::ceilingGain (ceilingDb);
        auto buffer = makeBuffer ({ 1.5f, -1.5f, 0.5f }, { -0.5f, 1.5f, -1.5f });

        BP_CHECK (SoftClip::processBuffer (buffer, 3, c, true));
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 3; ++i)
                BP_CHECK (std::abs (buffer.getSample (channel, i)) <= c);
    }

    // numSamples only covers the requested region; later samples are untouched.
    {
        auto buffer = makeBuffer ({ 2.0f, 0.1f }, { 2.0f, 0.1f });
        SoftClip::processBuffer (buffer, 1, ceiling, true);

        BP_CHECK_NEAR (buffer.getSample (0, 0), SoftClip::shape (2.0f, ceiling), 1.0e-6);
        BP_CHECK_EQ (buffer.getSample (0, 1), 0.1f);
        BP_CHECK_EQ (buffer.getSample (1, 1), 0.1f);
    }
}
