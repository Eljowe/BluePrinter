#include "MetronomePlayer.h"

#include <algorithm>
#include <cmath>

MetronomePlayer::MetronomePlayer()
{
    // A clump of clicks can overlap at high BPM; reserve a typical ceiling
    // so the audio thread doesn't allocate on the common path.
    activeClicks.reserve (16);
}

void MetronomePlayer::setContext (double newSampleRate, double newBpm, int newBeatsPerBar)
{
    sampleRate  = newSampleRate;
    bpm         = newBpm;
    beatsPerBar = newBeatsPerBar;
}

void MetronomePlayer::reset()
{
    activeClicks.clear();
    lastStartPos = 0;
}

void MetronomePlayer::renderTail (juce::AudioBuffer<float>& buffer,
                                  ActiveClick& ac,
                                  juce::int64 startPos,
                                  juce::int64 endPos,
                                  int numChannels)
{
    if (ac.buffer == nullptr || ac.readPos >= static_cast<int> (ac.buffer->size()))
        return;

    // Where this click's unplayed samples would land.
    const juce::int64 head = juce::jmax (startPos, ac.nextSample);
    const juce::int64 tailEnd = ac.nextSample
                              + (static_cast<int> (ac.buffer->size()) - ac.readPos);
    if (tailEnd <= startPos)
        return;

    const juce::int64 inBlockEnd = juce::jmin (endPos, tailEnd);
    if (inBlockEnd <= head)
        return;

    const int blockOff = static_cast<int> (head - startPos);
    const int count    = static_cast<int> (inBlockEnd - head);
    for (int j = 0; j < count; ++j)
    {
        const float sample = (*ac.buffer)[static_cast<size_t> (ac.readPos + j)];
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addSample (ch, blockOff + j, sample);
    }

    ac.readPos    += count;
    ac.nextSample += count;
}

void MetronomePlayer::render (juce::AudioBuffer<float>& buffer,
                              juce::int64 startPos,
                              int numSamples,
                              const std::shared_ptr<const std::vector<float>>& tick,
                              const std::shared_ptr<const std::vector<float>>& accent)
{
    // Copy the shared_ptrs once per call so a message-thread resynth can
    // never invalidate the buffers mid-render.
    const auto normalClick = tick;
    const auto accentClick = accent;

    if (bpm <= 0.0 || sampleRate <= 0.0)
        return;

    const double samplesPerBeat = 60.0 / bpm * sampleRate;
    if (samplesPerBeat <= 0.0)
        return;

    const int numChannels = buffer.getNumChannels();
    if (numChannels <= 0)
        return;

    // A backward jump means the clock was reset (a new count-in / capture):
    // drop any click still ringing from the old clock so it cannot land on
    // the new beat grid.
    if (startPos < lastStartPos)
        activeClicks.clear();
    lastStartPos = startPos;

    const juce::int64 endPos = startPos + numSamples;

    // 1. Ring out clicks that started in earlier blocks.
    for (auto& ac : activeClicks)
        renderTail (buffer, ac, startPos, endPos, numChannels);

    activeClicks.erase (std::remove_if (activeClicks.begin(), activeClicks.end(),
                                        [](const ActiveClick& ac)
                                        {
                                            return ac.buffer == nullptr
                                                || ac.readPos
                                                    >= static_cast<int> (ac.buffer->size());
                                        }),
                        activeClicks.end());

    if ((normalClick == nullptr || normalClick->empty())
     && (accentClick == nullptr || accentClick->empty()))
        return;

    // Accent the first beat of every bar. Falls back to 4-beat bars when
    // the count-in is disabled so the accent still works while recording.
    const int barLength = juce::jmax (1, beatsPerBar);

    const int firstBeat = static_cast<int> (std::ceil (static_cast<double> (startPos) / samplesPerBeat));
    const int lastBeat  = static_cast<int> (std::floor (static_cast<double> (endPos) / samplesPerBeat));

    for (int beat = firstBeat; beat <= lastBeat; ++beat)
    {
        const juce::int64 beatSample = static_cast<juce::int64> (beat * samplesPerBeat);
        if (beatSample < startPos || beatSample >= endPos)
            continue;

        const bool isAccent = (beat % barLength == 0)
            && accentClick != nullptr && ! accentClick->empty();
        if (isAccent == false
         && (normalClick == nullptr || normalClick->empty()))
            continue;

        ActiveClick ac;
        ac.buffer     = isAccent ? accentClick : normalClick;
        ac.nextSample = beatSample;
        ac.readPos    = 0;
        renderTail (buffer, ac, startPos, endPos, numChannels);
        activeClicks.push_back (ac);
    }
}
