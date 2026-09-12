#include "TestRunner.h"
#include "PreRollMath.h"

BP_TEST (PreRoll_completesAfterTheConfiguredBeats)
{
    // 120 BPM at 48 kHz: 24000 samples per beat.
    BP_CHECK (! PreRoll::isComplete (0,     48000.0, 120.0f, 4));
    BP_CHECK (! PreRoll::isComplete (24000, 48000.0, 120.0f, 4));   // 1 beat
    BP_CHECK (! PreRoll::isComplete (72000, 48000.0, 120.0f, 4));   // 3 beats
    BP_CHECK (PreRoll::isComplete (96000,  48000.0, 120.0f, 4));    // 4 beats
    BP_CHECK (PreRoll::isComplete (120000, 48000.0, 120.0f, 4));
}

BP_TEST (PreRoll_completesImmediatelyWithNothingToCount)
{
    BP_CHECK (PreRoll::isComplete (0, 48000.0, 120.0f, 0));
    BP_CHECK (PreRoll::isComplete (0, 48000.0, 0.0f, 4));
    BP_CHECK (PreRoll::isComplete (0, 0.0, 120.0f, 4));
}
