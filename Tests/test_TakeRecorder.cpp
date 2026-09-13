#include "TestRunner.h"
#include "TakeRecorder.h"

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

BP_TEST (TakeRecorder_writeFillsTheBufferAndRequestsFinalize)
{
    TakeRecorder rec;
    auto record = ramp (1, 4, 0.0f);
    auto source = ramp (1, 4, 100.0f);

    rec.beginFresh (record);
    BP_CHECK (rec.isRecordingRequested());
    BP_CHECK (rec.getState() == TakeRecorder::State::Recording);

    rec.write (record, 4, source, 4);

    BP_CHECK (! rec.isRecordingRequested());
    BP_CHECK (rec.getState() == TakeRecorder::State::Idle);
    BP_CHECK (rec.isFinalizePending());
    BP_CHECK_EQ (rec.getWritePos(), static_cast<int64_t> (4));
    BP_CHECK_NEAR (record.getSample (0, 0), 100.0f, 0.0001f);

    BP_CHECK (rec.consumeFinalizePending());
    BP_CHECK (! rec.consumeFinalizePending());
}

BP_TEST (TakeRecorder_aNewTakeInvalidatesThePendingOne)
{
    TakeRecorder rec;
    auto record = ramp (1, 4, 0.0f);

    rec.setPendingTake (4);
    BP_CHECK (rec.isTakePending());

    rec.beginFresh (record);
    BP_CHECK (! rec.isTakePending());
    BP_CHECK_EQ (rec.getTakeLength(), static_cast<int64_t> (0));
}

BP_TEST (TakeRecorder_reviewPlaysOnceThenStops)
{
    TakeRecorder rec;
    auto record = ramp (1, 4, 0.0f); // 0,1,2,3
    rec.setPendingTake (4);

    BP_CHECK (rec.canStartReview());
    rec.startReview();
    BP_CHECK (rec.isReviewPlaying());

    juce::AudioBuffer<float> dest (1, 4);
    dest.clear();
    BP_CHECK (rec.renderReview (dest, record, 4));
    BP_CHECK_NEAR (dest.getSample (0, 2), 2.0f, 0.0001f);

    // readPos reached the end: review stopped itself.
    BP_CHECK (! rec.isReviewPlaying());
    BP_CHECK_EQ (rec.getReviewPos(), static_cast<int64_t> (0));
}

BP_TEST (TakeRecorder_overdubKeepsTheTakeAndCapturesAfterIt)
{
    TakeRecorder rec;
    auto record = ramp (1, 8, 0.0f);
    rec.setPendingTake (4);
    rec.setOverdub (true);
    BP_CHECK (rec.wantsOverdub());

    rec.beginOverdub();
    BP_CHECK (rec.isOverdubCapture());
    BP_CHECK_EQ (rec.getWritePos(), static_cast<int64_t> (4));
    BP_CHECK (rec.isTakePending());

    // The monitor adds the looping take over the output.
    juce::AudioBuffer<float> dest (1, 4);
    dest.clear();
    rec.renderOverdubMonitor (dest, record, 4, 2);
    BP_CHECK_NEAR (dest.getSample (0, 1), 2.0f / 3.0f, 0.001f);

    // A layered capture can't start a review.
    BP_CHECK (! rec.canStartReview());
}

BP_TEST (TakeRecorder_clearPendingResetsEverything)
{
    TakeRecorder rec;
    rec.setPendingTake (4);
    rec.setTakePeaks ({ 0.5f, 0.25f });
    rec.setOverdub (true);
    rec.markOverdubPending (true);

    rec.clearPending();

    BP_CHECK (! rec.isTakePending());
    BP_CHECK (! rec.isReviewPlaying());
    BP_CHECK (! rec.isOverdubCapture());
    BP_CHECK (rec.getTakePeaks().empty());
    BP_CHECK (! rec.consumeOverdubPending());
}

BP_TEST (TakeRecorder_wantsOverdubNeedsATakeAndTheToggle)
{
    TakeRecorder rec;
    BP_CHECK (! rec.wantsOverdub());

    rec.setOverdub (true);
    BP_CHECK (! rec.wantsOverdub()); // no pending take

    rec.setPendingTake (4);
    BP_CHECK (rec.wantsOverdub());

    rec.setOverdub (false);
    BP_CHECK (! rec.wantsOverdub());
}

BP_TEST (TakeRecorder_countInIntentSurvivesUntilOverdubChoice)
{
    TakeRecorder rec;
    rec.markOverdubPending (true);
    rec.armCountIn();
    BP_CHECK (rec.isPreRollActive());
    BP_CHECK (rec.isActive());

    // endCountIn keeps the overdub intent for beginActualRecording.
    rec.endCountIn();
    BP_CHECK (! rec.isPreRollActive());
    BP_CHECK (rec.consumeOverdubPending());
    BP_CHECK (! rec.consumeOverdubPending());

    // cancelCountIn drops it (and goes idle).
    rec.markOverdubPending (true);
    rec.armCountIn();
    rec.cancelCountIn();
    BP_CHECK (! rec.isPreRollActive());
    BP_CHECK (! rec.consumeOverdubPending());
    BP_CHECK (rec.getState() == TakeRecorder::State::Idle);
}
