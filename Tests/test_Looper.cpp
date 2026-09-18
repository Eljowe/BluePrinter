#include "TestRunner.h"
#include "Looper.h"

namespace
{
juce::AudioBuffer<float> ramp (int channels, int samples, float startValue)
{
    juce::AudioBuffer<float> buffer (channels, samples);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < samples; ++i)
            buffer.setSample (ch, i, startValue + static_cast<float> (i));
    return buffer;
}
}

BP_TEST (Looper_freshCaptureAppendsAndAutoStopsAtTheTarget)
{
    Looper loop;
    auto record = ramp (1, 16, 0.0f);
    auto source = ramp (1, 4, 100.0f);

    loop.armFresh();
    loop.armCaptureNow();
    BP_CHECK (loop.isCaptureArmed());

    loop.captureBlock (record, 16, source, 4, 3); // target 3, block writes 4
    BP_CHECK_NEAR (record.getSample (0, 0), 100.0f, 0.0001f);
    BP_CHECK_EQ (loop.getLength(), static_cast<int64_t> (4));
    BP_CHECK (! loop.isCaptureArmed());
    BP_CHECK (loop.isAutoStopPending());
    BP_CHECK (loop.consumeAutoStopPending());
}

BP_TEST (Looper_overdubKeepsTheLoopLengthAndWritesAfterIt)
{
    Looper loop;
    auto record = ramp (1, 16, 0.0f);
    auto layer = ramp (1, 4, 200.0f);

    loop.setGridTrimmed (4);
    loop.setOverdub (true);
    loop.armOverdub (false);
    BP_CHECK (loop.isOverdubCapture());
    BP_CHECK_EQ (loop.getOverdubWritePos(), static_cast<int64_t> (4));
    loop.armCaptureNow();

    loop.captureBlock (record, 16, layer, 4, 0);
    BP_CHECK_EQ (loop.getLength(), static_cast<int64_t> (4));       // unchanged
    BP_CHECK_EQ (loop.getOverdubWritePos(), static_cast<int64_t> (8));
    BP_CHECK_NEAR (record.getSample (0, 4), 200.0f, 0.0001f);
}

BP_TEST (Looper_renderPlaybackAddsTheLoopAndStopsOneShot)
{
    Looper loop;
    auto record = ramp (1, 4, 0.0f); // 0,1,2,3
    loop.setGridTrimmed (4);
    loop.setPlaying (true);
    BP_CHECK (loop.isPlaying());

    juce::AudioBuffer<float> dest (1, 4);
    dest.clear();
    float peak = 0.0f;
    BP_CHECK (loop.renderPlayback (dest, record, 4, 1.0f, 0, peak));
    BP_CHECK_NEAR (dest.getSample (0, 2), 2.0f, 0.0001f);
    BP_CHECK_NEAR (peak, 3.0f, 0.0001f);

    // One-shot: reaching the end stops playback.
    loop.setLooping (false);
    loop.setPlaying (true);
    juce::AudioBuffer<float> dest2 (1, 4);
    dest2.clear();
    loop.renderPlayback (dest2, record, 4, 1.0f, 0, peak);
    BP_CHECK (! loop.isPlaying());
}

BP_TEST (Looper_countInBeginCaptureResetsFreshButKeepsOverdub)
{
    Looper loop;

    loop.setGridTrimmed (4);
    loop.armFresh();
    loop.armCountIn();
    BP_CHECK (loop.isPreRollActive());
    loop.beginCapture();
    BP_CHECK (loop.isCaptureArmed());
    BP_CHECK_EQ (loop.getLength(), static_cast<int64_t> (0)); // fresh resets

    loop.setGridTrimmed (4);
    loop.setOverdub (true);
    loop.armOverdub (true);
    BP_CHECK (! loop.isPlaying()); // silent through the count-in
    loop.armCountIn();
    loop.beginCapture();
    BP_CHECK_EQ (loop.getLength(), static_cast<int64_t> (4)); // overdub keeps
    BP_CHECK (loop.isPlaying());                              // starts from top
    BP_CHECK_EQ (loop.getPosition(), 0.0);
}

BP_TEST (Looper_gridTrimAndCropAreReversible)
{
    Looper loop;
    loop.setGridTrimmed (8);
    BP_CHECK_EQ (loop.getStart(), static_cast<int64_t> (0));
    BP_CHECK_EQ (loop.getLength(), static_cast<int64_t> (8));
    BP_CHECK_EQ (loop.getFullLength(), static_cast<int64_t> (8));

    // sampleRate 4 @ 120 bpm => 2 samples per beat; 8 samples = 4 beats.
    BP_CHECK (loop.setCrop (1, 1, 4.0, 120.0f, {}));
    BP_CHECK_EQ (loop.getCropStartBeats(), 1);
    BP_CHECK_EQ (loop.getCropEndBeats(), 1);
    BP_CHECK_EQ (loop.getStart(), static_cast<int64_t> (2));
    BP_CHECK_EQ (loop.getLength(), static_cast<int64_t> (4));

    // Moving the start handle back restores the region it cut.
    BP_CHECK (loop.setCrop (0, 1, 4.0, 120.0f, {}));
    BP_CHECK_EQ (loop.getStart(), static_cast<int64_t> (0));
    BP_CHECK_EQ (loop.getLength(), static_cast<int64_t> (6));
}

BP_TEST (Looper_layerUndoRedoRestoresTheFullRegion)
{
    Looper loop;
    auto record = ramp (1, 16, 10.0f); // 10,11,12,...
    loop.setGridTrimmed (4);

    loop.pushUndoSnapshot (record);
    BP_CHECK (loop.isUndoAvailable());
    BP_CHECK (! loop.isRedoAvailable());

    for (int i = 0; i < 4; ++i)
        record.setSample (0, i, 99.0f);

    BP_CHECK (loop.undo (record));
    BP_CHECK_NEAR (record.getSample (0, 0), 10.0f, 0.0001f);
    BP_CHECK (loop.isRedoAvailable());

    BP_CHECK (loop.redo (record));
    BP_CHECK_NEAR (record.getSample (0, 0), 99.0f, 0.0001f);
    BP_CHECK (loop.isUndoAvailable());
}

BP_TEST (Looper_setLoadedPublishesAFreshLoopAndClearsHistory)
{
    Looper loop;
    loop.setGridTrimmed (4);
    loop.pushUndoSnapshot (ramp (1, 8, 0.0f));
    loop.setPlaying (true);
    BP_CHECK (loop.isUndoAvailable());

    loop.setLoaded (8);
    BP_CHECK_EQ (loop.getStart(), static_cast<int64_t> (0));
    BP_CHECK_EQ (loop.getLength(), static_cast<int64_t> (8));
    BP_CHECK_EQ (loop.getFullLength(), static_cast<int64_t> (8));
    BP_CHECK_EQ (loop.getCropStartBeats(), 0);
    BP_CHECK_EQ (loop.getCropEndBeats(), 0);
    BP_CHECK (! loop.isUndoAvailable());
    BP_CHECK (! loop.isRedoAvailable());
    BP_CHECK (! loop.isPlaying());
    BP_CHECK (loop.isIdle());
}

BP_TEST (Looper_clearAndOverdubSettingsResetState)
{
    Looper loop;
    loop.setGridTrimmed (4);
    loop.pushUndoSnapshot (ramp (1, 8, 0.0f));
    loop.armOverdub (false);
    loop.setPlaybackReverse (true);

    loop.setOverdub (true);
    BP_CHECK (! loop.isPlaybackReverse()); // overdub forces forward

    loop.setOverdub (false);
    loop.clear();
    BP_CHECK (! loop.hasLoop());
    BP_CHECK (! loop.isUndoAvailable());
    BP_CHECK (! loop.isRedoAvailable());
    BP_CHECK (loop.isIdle());
    BP_CHECK (loop.getPeaks().empty());
}
