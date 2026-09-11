#include "TestRunner.h"
#include "MidiChannelFilter.h"

BP_TEST (MidiChannelFilter_fullMaskAcceptsEveryChannel)
{
    for (int channel = 1; channel <= 16; ++channel)
        BP_CHECK (MidiChannelFilter::accepts (0xFFFF, channel));
}

BP_TEST (MidiChannelFilter_systemMessagesAlwaysPass)
{
    BP_CHECK (MidiChannelFilter::accepts (0x0000, 0));
    BP_CHECK (MidiChannelFilter::accepts (0x0000, -1));
}

BP_TEST (MidiChannelFilter_onlySetChannelsPass)
{
    BP_CHECK (! MidiChannelFilter::accepts (0x0000, 1));
    BP_CHECK (MidiChannelFilter::accepts (0x0001, 1));
    BP_CHECK (! MidiChannelFilter::accepts (0x0001, 2));
    BP_CHECK (! MidiChannelFilter::accepts (0x8000, 15));
    BP_CHECK (MidiChannelFilter::accepts (0x8000, 16));
}
