#include "TestRunner.h"
#include "TakeRecorder.h"
#include "LoopPlayback.h"
#include <thread>

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

BP_TEST (TakeRecorder_trimIsNonDestructiveAndSaveCopiesOnlyTheKeptRegion)
{
    TakeRecorder rec;
    auto original = take (2, 10, 100.0f);
    rec.addTake (original, 10, { 0.5f });
    BP_CHECK (rec.setTrim (2, 7));
    BP_CHECK_EQ (rec.getSelectedTakeLength(), 10);
    BP_CHECK (rec.getSelectedTakeAudio() == original);
    auto kept = rec.getSelectedTrimmedAudio();
    BP_CHECK_EQ (kept->getNumSamples(), 5);
    BP_CHECK_EQ (kept->getNumChannels(), 2);
    BP_CHECK_NEAR (kept->getSample (1, 0), 102.0f, 0.0001f);
    BP_CHECK_NEAR (kept->getSample (1, 4), 106.0f, 0.0001f);
    BP_CHECK_NEAR (original->getSample (0, 9), 109.0f, 0.0001f);
    BP_CHECK (rec.setTrim (0, 10));
    BP_CHECK (rec.getSelectedTrimmedAudio() == original);
}

BP_TEST (TakeRecorder_reviewStartsSeeksAndStopsWithinTrim)
{
    TakeRecorder rec;
    rec.addTake (take (1, 12, 100.0f), 12, {});
    rec.setTrim (2, 10);
    rec.startReview();
    BP_CHECK_EQ (rec.getReviewPos(), 2);
    juce::AudioBuffer<float> dest (2, 3);
    dest.clear();
    rec.renderReview (dest, 3);
    BP_CHECK_NEAR (dest.getSample (0, 0), 102.0f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (1, 0), 0.0f, 0.0001f);

    rec.seekReview (7);
    rec.seekReview (8); // The newest queued seek wins.
    rec.renderReview (dest, 3);
    BP_CHECK_NEAR (dest.getSample (0, 0), 108.0f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (0, 1), 109.0f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (0, 2), 0.0f, 0.0001f);
    BP_CHECK (! rec.isReviewPlaying());
    BP_CHECK_EQ (rec.getReviewPos(), 10);

    rec.startReview (0);
    BP_CHECK_EQ (rec.getReviewPos(), 2);
    rec.seekReview (1000);
    rec.renderReview (dest, 3);
    BP_CHECK_NEAR (dest.getSample (0, 0), 109.0f, 0.0001f);
    BP_CHECK (! rec.isReviewPlaying());
    rec.stopReview();
    BP_CHECK_EQ (rec.getReviewPos(), 2);
}

BP_TEST (TakeRecorder_trimClampsAndStaysWithEachTake)
{
    TakeRecorder rec;
    BP_CHECK (! rec.setTrim (0, 5));
    rec.addTake (take (1, 10, 0.0f), 10, {});
    const auto firstId = rec.getSelectedTakeId();
    rec.setTrim (3, 8);
    rec.addTake (take (1, 5, 0.0f), 5, {});
    BP_CHECK_EQ (rec.getTrimStart(), 0);
    BP_CHECK_EQ (rec.getTrimEnd(), 5);
    rec.selectTake (firstId);
    BP_CHECK_EQ (rec.getTrimStart(), 3);
    BP_CHECK_EQ (rec.getTrimEnd(), 8);
    rec.setTrim (100, -5);
    BP_CHECK_EQ (rec.getTrimStart(), 9);
    BP_CHECK_EQ (rec.getTrimEnd(), 10);
    rec.setTrim (-4, 50);
    BP_CHECK_EQ (rec.getTrimStart(), 0);
    BP_CHECK_EQ (rec.getTrimEnd(), 10);
    rec.armCountIn();
    BP_CHECK (! rec.setTrim (2, 3));
    rec.startReview();
    BP_CHECK (! rec.isReviewPlaying());
    rec.cancelCountIn();
}

BP_TEST (TakeRecorder_trimmedOverdubWrapsOnlyInsideKeptRegionAndUndoRestoresAudio)
{
    TakeRecorder rec;
    auto original = take (1, 8, 0.0f);
    rec.addTake (original, 8, { 0.5f });
    rec.setTrim (2, 5);
    auto record = ramp (1, 16, 0.0f);
    rec.beginOverdub (record);
    BP_CHECK_EQ (rec.getWritePos(), 8); // Layer remains after the FULL source.
    juce::AudioBuffer<float> monitor (1, 6);
    monitor.clear();
    rec.renderOverdubMonitor (monitor, 6, 0);
    BP_CHECK_NEAR (monitor.getSample (0, 0), 2.0f, 0.0001f);
    BP_CHECK_NEAR (monitor.getSample (0, 3), 2.0f, 0.0001f);
    auto layer = ramp (1, 4, 10.0f);
    rec.write (record, 16, layer, 4);
    BP_CHECK (! rec.setTrim (0, 8));
    rec.finish();
    rec.consumeOverdubCapture();
    LoopPlayback::mixLayer (record, rec.getTrimStart(), rec.getTrimEnd() - rec.getTrimStart(), 8, 4, 1.0f);
    auto mixed = std::make_shared<juce::AudioBuffer<float>> (1, 8);
    mixed->copyFrom (0, 0, record, 0, 0, 8);
    rec.replaceSelectedTake (mixed, 8, { 1.0f });
    BP_CHECK_NEAR (mixed->getSample (0, 0), 0.0f, 0.0001f);
    BP_CHECK_NEAR (mixed->getSample (0, 2), 25.0f, 0.0001f);
    BP_CHECK_NEAR (mixed->getSample (0, 5), 5.0f, 0.0001f);
    BP_CHECK (rec.canUndoOverdub());
    rec.setTrim (1, 6);
    BP_CHECK (rec.undoOverdub());
    BP_CHECK (rec.getSelectedTakeAudio() == original);
    BP_CHECK_EQ (rec.getTrimStart(), 1);
    BP_CHECK_EQ (rec.getTrimEnd(), 6);
    BP_CHECK_NEAR (rec.getSelectedTakePeaks()[0], 0.5f, 0.0001f);
    BP_CHECK (! rec.canUndoOverdub());
}

BP_TEST (TakeRecorder_undoHistoryIsPerTakeAndBlockedDuringPlaybackOrCapture)
{
    TakeRecorder rec;
    rec.addTake (take (1, 4, 0.0f), 4, {});
    const auto firstId = rec.getSelectedTakeId();
    rec.replaceSelectedTake (take (1, 4, 10.0f), 4, {});
    rec.replaceSelectedTake (take (1, 4, 20.0f), 4, {});
    rec.addTake (take (1, 4, 30.0f), 4, {});
    BP_CHECK (! rec.canUndoOverdub());
    rec.selectTake (firstId);
    rec.startReview();
    BP_CHECK (! rec.undoOverdub());
    rec.stopReview();
    rec.armCountIn();
    BP_CHECK (! rec.undoOverdub());
    rec.cancelCountIn();
    BP_CHECK (rec.undoOverdub());
    BP_CHECK_NEAR (rec.getSelectedTakeAudio()->getSample (0, 0), 10.0f, 0.0001f);
    BP_CHECK (rec.undoOverdub());
    BP_CHECK_NEAR (rec.getSelectedTakeAudio()->getSample (0, 0), 0.0f, 0.0001f);
    BP_CHECK (! rec.undoOverdub());
    rec.replaceSelectedTake (take (1, 4, 40.0f), 4, {});
    rec.deleteTake (firstId);
    BP_CHECK (! rec.canUndoOverdub());
    rec.clearTakes();
    BP_CHECK (! rec.canUndoOverdub());
}

BP_TEST (TakeRecorder_undoHistoryBoundsLayersAndMemory)
{
    TakeRecorder rec;
    rec.addTake (take (1, 4, 0.0f), 4, {});
    for (int i = 1; i <= 12; ++i)
        rec.replaceSelectedTake (take (1, 4, static_cast<float> (i)), 4, {});
    int undos = 0;
    while (rec.undoOverdub())
        ++undos;
    BP_CHECK_EQ (undos, TakeRecorder::maxUndoLayers);
    BP_CHECK_NEAR (rec.getSelectedTakeAudio()->getSample (0, 0), 2.0f, 0.0001f);

    rec.clearTakes();
    const int samples = static_cast<int> (TakeRecorder::maxUndoBytes / sizeof (float) + 1);
    rec.addTake (std::make_shared<juce::AudioBuffer<float>> (1, samples), samples, {});
    rec.replaceSelectedTake (take (1, 4, 0.0f), 4, {});
    BP_CHECK (! rec.canUndoOverdub()); // A snapshot larger than the budget is not retained.

    rec.clearTakes();
    const int halfBudgetSamples = static_cast<int> (TakeRecorder::maxUndoBytes / (2 * sizeof (float)) + 1);
    rec.addTake (std::make_shared<juce::AudioBuffer<float>> (1, halfBudgetSamples), halfBudgetSamples, {});
    const auto olderId = rec.getSelectedTakeId();
    rec.replaceSelectedTake (take (1, 4, 1.0f), 4, {});
    rec.addTake (std::make_shared<juce::AudioBuffer<float>> (1, halfBudgetSamples), halfBudgetSamples, {});
    const auto newerId = rec.getSelectedTakeId();
    rec.replaceSelectedTake (take (1, 4, 2.0f), 4, {});
    BP_CHECK (rec.canUndoOverdub());
    rec.selectTake (olderId);
    BP_CHECK (! rec.canUndoOverdub()); // Cumulative byte cap evicts oldest, not newest.
    rec.selectTake (newerId);
    BP_CHECK (rec.undoOverdub());
    BP_CHECK_EQ (rec.getSelectedTakeLength(), halfBudgetSamples);
}

BP_TEST (TakeRecorder_publicationCanOverlapAudioReview)
{
    TakeRecorder rec;
    rec.addTake (take (1, 4096, 0.0f), 4096, {});
    std::atomic<bool> running { true };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> dest (1, 32);
        while (running.load())
            rec.renderReview (dest, 32);
    });
    for (int i = 0; i < 100; ++i)
    {
        rec.startReview();
        rec.setTrim (i, 4096 - i);
        rec.replaceSelectedTake (take (1, 4096, static_cast<float> (i)), 4096, {});
    }
    running.store (false);
    audio.join();
    BP_CHECK_EQ (rec.getTrimStart(), 99);
    BP_CHECK_EQ (rec.getSelectedTakeLength(), 4096);
}
