#include "TestRunner.h"
#include "MetronomePlayer.h"

#include <cmath>

namespace
{
std::shared_ptr<const std::vector<float>> makeClickBuffer (int length, float value)
{
    return std::make_shared<const std::vector<float>> (static_cast<size_t> (length), value);
}

float sumAbs (const juce::AudioBuffer<float>& b)
{
    float s = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            s += std::abs (b.getSample (ch, i));
    return s;
}
}

BP_TEST (MetronomePlayer_rendersAndRingsOutClicks)
{
    MetronomePlayer player;
    player.setContext (48000.0, 120.0, 4); // beat = 24000 samples

    auto tick = makeClickBuffer (100, 1.0f);
    auto accent = makeClickBuffer (100, 1.0f);

    // Block 0..63: beat 0 lands at sample 0 (accent) and 64 of its 100
    // samples render here.
    juce::AudioBuffer<float> first (1, 64);
    first.clear();
    player.render (first, 0, 64, tick, accent);
    BP_CHECK_NEAR (first.getSample (0, 0), 1.0f, 0.001f);
    BP_CHECK_NEAR (sumAbs (first), 64.0f, 0.5f);

    // Block 64..127: the remaining 36-sample tail rings out.
    juce::AudioBuffer<float> second (1, 64);
    second.clear();
    player.render (second, 64, 64, tick, accent);
    BP_CHECK_NEAR (sumAbs (second), 36.0f, 0.5f);

    // Beat 1 (sample 24000) fires the softer tick.
    juce::AudioBuffer<float> beatBlock (1, 100);
    beatBlock.clear();
    player.render (beatBlock, 24000, 100, tick, accent);
    BP_CHECK_NEAR (beatBlock.getSample (0, 0), 1.0f, 0.001f);

    // A block with no beat and no ringing click stays silent.
    juce::AudioBuffer<float> quiet (1, 1000);
    quiet.clear();
    player.render (quiet, 1000, 1000, tick, accent);
    BP_CHECK_NEAR (sumAbs (quiet), 0.0f, 0.0001f);
}

BP_TEST (MetronomePlayer_resetAndBackwardJumpDropRingingClicks)
{
    MetronomePlayer player;
    player.setContext (48000.0, 120.0, 4);

    auto tick = makeClickBuffer (100, 1.0f);
    auto accent = makeClickBuffer (100, 1.0f);

    // Beat 1 at sample 24000; 64 of its 100 samples render, 36 ring.
    juce::AudioBuffer<float> first (1, 64);
    first.clear();
    player.render (first, 24000, 64, tick, accent);

    // A backward jump (start < last) is a clock reset: the tail must not
    // appear (there is no beat in 100..163 either).
    juce::AudioBuffer<float> jumped (1, 64);
    jumped.clear();
    player.render (jumped, 100, 64, tick, accent);
    BP_CHECK_NEAR (sumAbs (jumped), 0.0f, 0.0001f);

    // reset() also clears the ringing state.
    juce::AudioBuffer<float> a (1, 64);
    a.clear();
    player.render (a, 24000, 64, tick, accent);
    player.reset();

    juce::AudioBuffer<float> b (1, 64);
    b.clear();
    player.render (b, 24064, 64, tick, accent); // no beat in range
    BP_CHECK_NEAR (sumAbs (b), 0.0f, 0.0001f);
}
