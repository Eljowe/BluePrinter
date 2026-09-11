#include "TestRunner.h"
#include "LooperGridMath.h"

namespace
{
constexpr double kSampleRate = 44100.0;
constexpr float  kBpm        = 120.0f;
// 120 BPM at 44.1 kHz: beat = 22050, bar (4 beats) = 88200 samples.
constexpr int64_t kBeat = 22050;
constexpr int64_t kBar  = 88200;
}

BP_TEST (LooperGrid_padsAShortCaptureToTheNearestBar)
{
    // Just over half a bar rounds down to one bar, i.e. the capture is
    // padded out to a full bar.
    BP_CHECK_EQ (LooperGrid::computeLength (50000, kSampleRate, kBpm, 1000000), kBar);
}

BP_TEST (LooperGrid_keepsAnExactBarCapture)
{
    BP_CHECK_EQ (LooperGrid::computeLength (kBar, kSampleRate, kBpm, 1000000), kBar);
}

BP_TEST (LooperGrid_truncatesAnOverlongCaptureToTheNearestBar)
{
    // Two bars plus a bit snaps to exactly two bars (audio past the
    // boundary is truncated by the caller).
    BP_CHECK_EQ (LooperGrid::computeLength (190000, kSampleRate, kBpm, 1000000), 2 * kBar);
}

BP_TEST (LooperGrid_fallsBackToWholeBeatsUnderHalfABar)
{
    // 30000 samples is closer to 0 bars than 1, so the bar snap yields 0
    // and the beat snap takes over: 30000 / 22050 = 1.36 -> one beat.
    BP_CHECK_EQ (LooperGrid::computeLength (30000, kSampleRate, kBpm, 1000000), kBeat);
}

BP_TEST (LooperGrid_clampsToMaxSamples)
{
    BP_CHECK_EQ (LooperGrid::computeLength (1000000000LL, kSampleRate, kBpm, 100),
                 static_cast<int64_t> (100));
}

BP_TEST (LooperGrid_rejectsInvalidInput)
{
    BP_CHECK_EQ (LooperGrid::computeLength (0, kSampleRate, kBpm, 1000000), static_cast<int64_t> (0));
    BP_CHECK_EQ (LooperGrid::computeLength (-5, kSampleRate, kBpm, 1000000), static_cast<int64_t> (0));
    BP_CHECK_EQ (LooperGrid::computeLength (50000, 0.0, kBpm, 1000000), static_cast<int64_t> (0));
}

BP_TEST (LooperGrid_computesFixedLengthInBars)
{
    // 4 bars at 120 BPM / 44.1 kHz = 4 bars * 4 beats * 22050 = 352800.
    BP_CHECK_EQ (LooperGrid::computeFixedLengthSamples (4, kSampleRate, kBpm), 4 * 4 * kBeat);
    BP_CHECK_EQ (LooperGrid::computeFixedLengthSamples (1, kSampleRate, kBpm), kBar);
    BP_CHECK_EQ (LooperGrid::computeFixedLengthSamples (0, kSampleRate, kBpm), static_cast<int64_t> (0));
    BP_CHECK_EQ (LooperGrid::computeFixedLengthSamples (-2, kSampleRate, kBpm), static_cast<int64_t> (0));
    BP_CHECK_EQ (LooperGrid::computeFixedLengthSamples (4, 0.0, kBpm), static_cast<int64_t> (0));
}

BP_TEST (LooperGrid_computesBeatCrop)
{
    // 2-bar loop (8 beats); trim one beat off each end.
    const auto crop = LooperGrid::computeCrop (2 * kBar, kSampleRate, kBpm, 1, 1);
    BP_CHECK_EQ (crop.startBeats, 1);
    BP_CHECK_EQ (crop.endBeats, 1);
    BP_CHECK_EQ (crop.startSamples, kBeat);
    BP_CHECK_EQ (crop.lengthSamples, 2 * kBar - 2 * kBeat);

    // No crop.
    const auto full = LooperGrid::computeCrop (kBar, kSampleRate, kBpm, 0, 0);
    BP_CHECK_EQ (full.startSamples, static_cast<int64_t> (0));
    BP_CHECK_EQ (full.lengthSamples, kBar);
}

BP_TEST (LooperGrid_cropClampsToKeepAtLeastOneBeat)
{
    // 1 bar (4 beats): asking to trim 3 beats off the front clamps to 3,
    // leaving one beat; a further end trim can't cross the start.
    const auto crop = LooperGrid::computeCrop (kBar, kSampleRate, kBpm, 3, 4);
    BP_CHECK_EQ (crop.startBeats, 3);
    BP_CHECK_EQ (crop.endBeats, 0);
    BP_CHECK_EQ (crop.startSamples, 3 * kBeat);
    BP_CHECK_EQ (crop.lengthSamples, kBeat);

    // A start past the loop clamps to the last beat.
    const auto past = LooperGrid::computeCrop (kBar, kSampleRate, kBpm, 99, 0);
    BP_CHECK_EQ (past.startBeats, 3);
    BP_CHECK_EQ (past.endBeats, 0);
}

BP_TEST (LooperGrid_cropRejectsInvalidInput)
{
    BP_CHECK_EQ (LooperGrid::computeCrop (0, kSampleRate, kBpm, 1, 1).lengthSamples,
                 static_cast<int64_t> (0));
    BP_CHECK_EQ (LooperGrid::computeCrop (kBar, 0.0, kBpm, 1, 1).lengthSamples,
                 static_cast<int64_t> (0));
}
