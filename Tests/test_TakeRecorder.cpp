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

std::shared_ptr<juce::AudioBuffer<float>> take (int channels, int samples, float startValue)
{
    return std::make_shared<juce::AudioBuffer<float>> (ramp (channels, samples, startValue));
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

BP_TEST (TakeRecorder_newTakesAppendAndAutoSelect)
{
    TakeRecorder rec;
    rec.addTake (take (1, 4, 0.0f), 4, { 1.0f });
    BP_CHECK_EQ (rec.getTakeCount(), 1);
    rec.addTake (take (1, 8, 0.0f), 8, { 2.0f });

    BP_CHECK_EQ (rec.getTakeCount(), 2);
    BP_CHECK_EQ (rec.getSelectedTakeLength(), static_cast<int64_t> (8)); // newest auto-selected
    BP_CHECK_EQ (rec.getTakeList().size(), 2u);
    BP_CHECK_NEAR (rec.getSelectedTakePeaks()[0], 2.0f, 0.0001f);

    // A fresh capture no longer invalidates the stack.
    auto record = ramp (1, 16, 0.0f);
    rec.beginFresh (record);
    BP_CHECK_EQ (rec.getTakeCount(), 2);
    rec.cancelCountIn();
}

BP_TEST (TakeRecorder_boundsDropOldestAndReport)
{
    TakeRecorder rec;
    for (int i = 0; i < TakeRecorder::maxTakes + 1; ++i)
        rec.addTake (take (1, 4, static_cast<float> (i)), 4, {});

    BP_CHECK_EQ (rec.getTakeCount(), TakeRecorder::maxTakes);
    BP_CHECK_EQ (rec.getTakeList().front().id, 2);   // id 1 (oldest) was dropped
    BP_CHECK_EQ (rec.consumeDroppedCount(), 1);
    BP_CHECK_EQ (rec.consumeDroppedCount(), 0);
}

BP_TEST (TakeRecorder_reviewPlaysTheSelectedTakeOnceThenStops)
{
    TakeRecorder rec;
    rec.addTake (take (1, 4, 0.0f), 4, {});      // 0,1,2,3
    rec.addTake (take (1, 4, 10.0f), 4, {});     // 10,11,12,13 (auto-selected)

    BP_CHECK (rec.canStartReview());
    rec.startReview();
    BP_CHECK (rec.isReviewPlaying());

    juce::AudioBuffer<float> dest (1, 4);
    dest.clear();
    BP_CHECK (rec.renderReview (dest, 4));
    BP_CHECK_NEAR (dest.getSample (0, 0), 10.0f, 0.0001f);

    BP_CHECK (! rec.isReviewPlaying());          // one-shot reached the end
}

BP_TEST (TakeRecorder_deleteSelectsTheNeighbour)
{
    TakeRecorder rec;
    rec.addTake (take (1, 4, 0.0f), 4, {});
    rec.addTake (take (1, 4, 0.0f), 4, {});
    rec.addTake (take (1, 4, 0.0f), 4, {});
    const auto ids = rec.getTakeList();
    rec.selectTake (ids[1].id);
    BP_CHECK_EQ (rec.getSelectedTakeId(), ids[1].id);

    rec.deleteTake (ids[1].id);
    BP_CHECK_EQ (rec.getTakeCount(), 2);
    BP_CHECK_EQ (rec.getSelectedTakeId(), ids[2].id); // next neighbour

    rec.deleteTake (ids[2].id);
    BP_CHECK_EQ (rec.getSelectedTakeId(), ids[0].id); // previous neighbour (last remaining)

    rec.deleteTake (ids[0].id);
    BP_CHECK_EQ (rec.getTakeCount(), 0);
    BP_CHECK (! rec.hasSelectedTake());
}

BP_TEST (TakeRecorder_overdubStagesSelectedAndReplacesIt)
{
    TakeRecorder rec;
    rec.addTake (take (1, 4, 0.0f), 4, { 1.0f });   // 0,1,2,3
    const int id = rec.getSelectedTakeId();
    rec.setOverdub (true);
    BP_CHECK (rec.wantsOverdub());

    // beginOverdub stages the selected take into the record buffer and parks
    // the write cursor after it.
    auto record = ramp (1, 16, 100.0f);
    rec.beginOverdub (record);
    BP_CHECK (rec.isOverdubCapture());
    BP_CHECK_EQ (rec.getWritePos(), static_cast<int64_t> (4));
    BP_CHECK_NEAR (record.getSample (0, 0), 0.0f, 0.0001f);

    // The monitor adds the looping take while the layer is captured.
    juce::AudioBuffer<float> dest (1, 4);
    dest.clear();
    rec.renderOverdubMonitor (dest, 4, 0);
    BP_CHECK_NEAR (dest.getSample (0, 1), 1.0f, 0.0001f);

    // The processor stops the capture, then publishes the mixed audio in place
    // (same take id) — mirror the real finalize order.
    rec.finish();
    rec.consumeOverdubCapture();
    rec.replaceSelectedTake (take (1, 4, 50.0f), 4, { 9.0f });
    BP_CHECK_EQ (rec.getSelectedTakeId(), id);
    BP_CHECK_NEAR (rec.getSelectedTakePeaks()[0], 9.0f, 0.0001f);
}

BP_TEST (TakeRecorder_clearTakesResetsTheStack)
{
    TakeRecorder rec;
    rec.addTake (take (1, 4, 0.0f), 4, { 1.0f });
    rec.startReview();
    rec.clearTakes();

    BP_CHECK_EQ (rec.getTakeCount(), 0);
    BP_CHECK (! rec.hasSelectedTake());
    BP_CHECK (! rec.isReviewPlaying());
    BP_CHECK (! rec.wantsOverdub());
    BP_CHECK (rec.getSelectedTakePeaks().empty());
}

BP_TEST (TakeRecorder_wantsOverdubNeedsASelectionAndTheToggle)
{
    TakeRecorder rec;
    BP_CHECK (! rec.wantsOverdub());

    rec.setOverdub (true);
    BP_CHECK (! rec.wantsOverdub()); // no take selected

    rec.addTake (take (1, 4, 0.0f), 4, {});
    BP_CHECK (rec.wantsOverdub());

    rec.setOverdub (false);
    BP_CHECK (! rec.wantsOverdub());
}

BP_TEST (TakeRecorder_countInIntentSurvivesUntilTheOverdubChoice)
{
    TakeRecorder rec;
    rec.markOverdubPending (true);
    rec.armCountIn();
    BP_CHECK (rec.isPreRollActive());
    BP_CHECK (rec.isActive());

    rec.endCountIn();
    BP_CHECK (! rec.isPreRollActive());
    BP_CHECK (rec.consumeOverdubPending());

    rec.markOverdubPending (true);
    rec.armCountIn();
    rec.cancelCountIn();
    BP_CHECK (! rec.consumeOverdubPending());
    BP_CHECK (rec.getState() == TakeRecorder::State::Idle);
}
