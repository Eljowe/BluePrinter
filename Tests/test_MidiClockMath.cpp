#include "TestRunner.h"
#include "MidiClockMath.h"

#include <vector>

BP_TEST (MidiClock_samplesPerPulseTracksTempo)
{
    // 120 BPM at 48 kHz: 0.5 s/beat -> 24000 samples, / 24 = 1000.
    BP_CHECK_NEAR (MidiClock::samplesPerPulse (48000.0, 120.0), 1000.0, 0.0001);
    // Unusable tempo / rate.
    BP_CHECK_NEAR (MidiClock::samplesPerPulse (48000.0, 0.0), 0.0, 0.0001);
    BP_CHECK_NEAR (MidiClock::samplesPerPulse (0.0, 120.0), 0.0, 0.0001);
}

BP_TEST (MidiClock_forEachPulseEmitsOneBeatOfPulses)
{
    // One beat = 24 pulses, one every 1000 samples; the pulse exactly at
    // the block end belongs to the next block.
    std::vector<int> offsets;
    const int count = MidiClock::forEachPulse (0, 24000, 48000.0, 120.0,
                                               [&offsets] (int off) { offsets.push_back (off); });
    BP_CHECK_EQ (count, 24);
    BP_CHECK_EQ (static_cast<int> (offsets.size()), 24);
    BP_CHECK_EQ (offsets.front(), 0);
    BP_CHECK_EQ (offsets.back(), 23000);

    // The next beat continues the same grid.
    std::vector<int> next;
    MidiClock::forEachPulse (24000, 24000, 48000.0, 120.0,
                             [&next] (int off) { next.push_back (off); });
    BP_CHECK_EQ (static_cast<int> (next.size()), 24);
    BP_CHECK_EQ (next.front(), 0);
    BP_CHECK_EQ (next.back(), 23000);
}

BP_TEST (MidiClock_forEachPulseHandlesPartialBlocksAndInvalidTempo)
{
    // A block that starts mid-grid emits only its own pulses at their
    // in-block offsets.
    std::vector<int> offsets;
    MidiClock::forEachPulse (500, 2000, 48000.0, 120.0,
                             [&offsets] (int off) { offsets.push_back (off); });
    BP_CHECK_EQ (static_cast<int> (offsets.size()), 2);
    BP_CHECK_EQ (offsets[0], 500);
    BP_CHECK_EQ (offsets[1], 1500);

    // Invalid tempo / rate emit nothing.
    int calls = 0;
    const auto counter = [&calls] (int) { ++calls; };
    BP_CHECK_EQ (MidiClock::forEachPulse (0, 1000, 48000.0, 0.0, counter), 0);
    BP_CHECK_EQ (MidiClock::forEachPulse (0, 1000, 0.0, 120.0, counter), 0);
    BP_CHECK_EQ (calls, 0);
}
