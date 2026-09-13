#include "TestRunner.h"
#include "StemCapture.h"

#include <initializer_list>

namespace
{
juce::AudioBuffer<float> makeMono (std::initializer_list<float> values)
{
    juce::AudioBuffer<float> buffer (1, static_cast<int> (values.size()));
    int i = 0;
    for (float v : values)
        buffer.setSample (0, i++, v);
    return buffer;
}
}

BP_TEST (StemCapture_armsDryPlusChains)
{
    StemCapture capture;
    BP_CHECK (capture.arm ("take", { "Clean", "Dirty" }, 1, 8));
    BP_CHECK (capture.isArmed());
    BP_CHECK_EQ (capture.getNumStems(), 3);
    BP_CHECK_EQ (capture.getStem (0).name, juce::String ("Dry"));
    BP_CHECK_EQ (capture.getStem (1).name, juce::String ("Clean"));
    BP_CHECK_EQ (capture.getStem (2).name, juce::String ("Dirty"));
    BP_CHECK (capture.getSource() == "take");
    BP_CHECK (! capture.hasStems());
}

BP_TEST (StemCapture_labelsUnnamedChains)
{
    StemCapture capture;
    BP_CHECK (capture.arm ("loop", { "" }, 1, 4));
    BP_CHECK_EQ (capture.getStem (1).name, juce::String ("Chain 1"));
}

BP_TEST (StemCapture_writesStemsAtPositionWithGain)
{
    StemCapture capture;
    BP_CHECK (capture.arm ("take", { "A" }, 1, 8));

    auto dry = makeMono ({ 1.0f, 0.0f });
    auto chain = makeMono ({ 0.5f, 0.25f });

    capture.writeStem (0, dry, 0, 0.5f, 2);
    capture.writeStem (1, chain, 0, 2.0f, 2);
    capture.finalise (2);

    BP_CHECK_NEAR (capture.getStem (0).buffer.getSample (0, 0), 0.5f, 0.0001f);
    BP_CHECK_NEAR (capture.getStem (0).buffer.getSample (0, 1), 0.0f, 0.0001f);
    BP_CHECK_NEAR (capture.getStem (1).buffer.getSample (0, 0), 1.0f, 0.0001f);
    BP_CHECK_NEAR (capture.getStem (1).buffer.getSample (0, 1), 0.5f, 0.0001f);
}

BP_TEST (StemCapture_stemsSumToMix)
{
    StemCapture capture;
    BP_CHECK (capture.arm ("take", { "A", "B" }, 1, 4));

    auto dry = makeMono ({ 0.1f, 0.2f, 0.3f, 0.4f });
    auto a   = makeMono ({ 0.5f, 0.5f, 0.5f, 0.5f });
    auto b   = makeMono ({ 0.25f, 0.0f, -0.25f, 0.0f });

    capture.writeStem (0, dry, 0, 1.0f, 4);
    capture.writeStem (1, a,   0, 0.5f, 4);
    capture.writeStem (2, b,   0, 1.0f, 4);
    capture.finalise (4);

    for (int i = 0; i < 4; ++i)
    {
        const float sum = capture.getStem (0).buffer.getSample (0, i)
                        + capture.getStem (1).buffer.getSample (0, i)
                        + capture.getStem (2).buffer.getSample (0, i);
        const float mix = dry.getSample (0, i)
                        + 0.5f * a.getSample (0, i)
                        + b.getSample (0, i);
        BP_CHECK_NEAR (sum, mix, 0.0001f);
    }
}

BP_TEST (StemCapture_finaliseTruncatesAndZeroPads)
{
    StemCapture capture;
    BP_CHECK (capture.arm ("loop", { "A" }, 1, 8));

    auto dry = makeMono ({ 1.0f, 1.0f, 1.0f, 1.0f });
    capture.writeStem (0, dry, 0, 1.0f, 4);

    // A grid trim that pads to 6: the tail must stay silent.
    capture.finalise (6);
    BP_CHECK_EQ (capture.getLength(), static_cast<int64_t> (6));
    BP_CHECK_NEAR (capture.getStem (0).buffer.getSample (0, 3), 1.0f, 0.0001f);
    BP_CHECK_NEAR (capture.getStem (0).buffer.getSample (0, 5), 0.0f, 0.0001f);

    // Truncating below the written length drops the tail.
    capture.arm ("loop", { "A" }, 1, 8);
    capture.writeStem (0, dry, 0, 1.0f, 4);
    capture.finalise (2);
    BP_CHECK_EQ (capture.getLength(), static_cast<int64_t> (2));
}

BP_TEST (StemCapture_writeClampsToCapacity)
{
    StemCapture capture;
    BP_CHECK (capture.arm ("take", {}, 1, 2));

    auto src = makeMono ({ 1.0f, 2.0f, 3.0f, 4.0f });
    capture.writeStem (0, src, 1, 1.0f, 4);
    capture.finalise (2);

    BP_CHECK_NEAR (capture.getStem (0).buffer.getSample (0, 0), 0.0f, 0.0001f);
    BP_CHECK_NEAR (capture.getStem (0).buffer.getSample (0, 1), 1.0f, 0.0001f);
}

BP_TEST (StemCapture_refusesOverMemoryCap)
{
    StemCapture capture;
    // One 4 GiB stem is far past maxBytes; arm must refuse and stay disarmed.
    const auto huge = static_cast<int64_t> (1024) * 1024 * 1024;
    BP_CHECK (! capture.arm ("take", { "A" }, 2, huge));
    BP_CHECK (! capture.isArmed());
    BP_CHECK_EQ (capture.getNumStems(), 0);
}

BP_TEST (StemCapture_clearDisarmsAndWriteIsNoOp)
{
    StemCapture capture;
    BP_CHECK (capture.arm ("take", { "A" }, 1, 4));
    capture.writeStem (0, makeMono ({ 1.0f }), 0, 1.0f, 1);
    capture.finalise (1);
    BP_CHECK (capture.hasStems());

    capture.clear();
    BP_CHECK (! capture.isArmed());
    BP_CHECK (! capture.hasStems());

    // A write after clear is ignored (the buffer still holds the old sample
    // but is not exported because the length is 0).
    capture.writeStem (0, makeMono ({ 9.0f }), 0, 1.0f, 1);
    BP_CHECK (! capture.hasStems());
}
