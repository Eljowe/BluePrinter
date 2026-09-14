#include "TestRunner.h"
#include "TapTempo.h"

// Tap-tempo averaging (0049). Timestamps are milliseconds.

BP_TEST (TapTempo_needsTwoTapsBeforeReporting)
{
    TapTempo tt;
    BP_CHECK (! tt.tap (0).has_value());      // first tap: no interval yet
    BP_CHECK_EQ (tt.tap (500).value(), 120);  // 500 ms interval = 120 BPM
}

BP_TEST (TapTempo_averagesTheRecentIntervals)
{
    TapTempo tt;
    // A steady 120 BPM (500 ms) should stay 120 as intervals accumulate.
    for (int i = 0; i <= 6; ++i)
        tt.tap (static_cast<int64_t> (i) * 500);

    // At most 5 intervals are retained.
    BP_CHECK_EQ (tt.getIntervalCount(), 5);
    BP_CHECK_EQ (tt.tap (7 * 500).value(), 120);
}

BP_TEST (TapTempo_resetsAfterASlowGap)
{
    TapTempo tt;
    tt.tap (0);
    BP_CHECK_EQ (tt.tap (500).value(), 120);

    // More than 2 s since the last tap: this tap starts a new measurement.
    BP_CHECK (! tt.tap (3000).has_value());
    BP_CHECK_EQ (tt.getIntervalCount(), 0);

    BP_CHECK_EQ (tt.tap (3500).value(), 120);
}

BP_TEST (TapTempo_dropsOutOfRangeTempoAndRecovers)
{
    TapTempo tt;
    tt.tap (0);
    // 100 ms interval = 600 BPM: out of range, so no BPM is reported and the
    // history is dropped.
    BP_CHECK (! tt.tap (100).has_value());
    BP_CHECK_EQ (tt.getIntervalCount(), 0);

    // Now tap a steady 120 BPM; the first pair reports 120.
    BP_CHECK_EQ (tt.tap (600).value(), 120);
}

BP_TEST (TapTempo_roundsToWholeBpm)
{
    TapTempo tt;
    tt.tap (0);
    BP_CHECK_EQ (tt.tap (480).value(), 125);  // 60000/480 = 125.0
    TapTempo rt;
    rt.tap (0);
    BP_CHECK_EQ (rt.tap (490).value(), 122);  // 60000/490 = 122.4 -> 122
}

BP_TEST (TapTempo_roundsAndKeepsTheReportedBpmInRange)
{
    // 1501 ms averages to 39.97 BPM, which rounds up to the 40 minimum.
    TapTempo low;
    low.tap (0);
    BP_CHECK_EQ (low.tap (1501).value(), 40);

    // 250 ms averages to exactly 240 BPM (the maximum).
    TapTempo high;
    high.tap (0);
    BP_CHECK_EQ (high.tap (250).value(), 240);

    // 1600 ms averages to 37.5 BPM: outside the window, so it resets.
    TapTempo slow;
    slow.tap (0);
    BP_CHECK (! slow.tap (1600).has_value());
    BP_CHECK_EQ (slow.getIntervalCount(), 0);

    // 249 ms averages to 240.96 BPM -> 241, outside the window: resets.
    TapTempo fast;
    fast.tap (0);
    BP_CHECK (! fast.tap (249).has_value());
}

BP_TEST (TapTempo_steadyTapsLandWithinOneBpm)
{
    // 160 BPM is 375 ms per beat; a little human jitter stays within 1 BPM.
    TapTempo tt;
    const int64_t taps[] = { 0, 375, 749, 1124, 1499, 1873 };
    std::optional<int> last;
    for (const auto t : taps)
        last = tt.tap (t);

    BP_CHECK (last.has_value());
    if (last.has_value())
        BP_CHECK (std::abs (*last - 160) <= 1);
}
