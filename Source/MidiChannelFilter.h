#pragma once

#include <JuceHeader.h>
#include <cstdint>

// Pure MIDI channel-filter logic, extracted from
// PluginChain::acceptsMidiChannel so the bitmask contract (channels
// 1..16, system messages always pass) can be unit tested.
namespace MidiChannelFilter
{
    // A channel-mask bit n (0-based) means channel n+1. A channel of 0
    // is a system message (clock, start, stop...) and always passes.
    inline bool accepts (uint16_t mask, int channel)
    {
        if (channel <= 0)
            return true;

        const uint16_t bit = static_cast<uint16_t> (1u << (channel - 1));
        return (mask & bit) != 0;
    }
}
