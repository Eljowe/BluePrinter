#include "MetronomePlayer.h"
#include "LooperGridMath.h"

#include <algorithm>
#include <cmath>

MetronomePlayer::MetronomePlayer()
{
    // A clump of clicks can overlap at high BPM (more with subdivisions);
    // reserve a typical ceiling so the audio thread doesn't allocate on the
    // common path.
    activeClicks.reserve (32);
}

void MetronomePlayer::setContext (double newSampleRate, double newBpm, int newBeatsPerBar, int newBeatUnit,
                                  int newSubdivision, uint16_t newAccentMask)
{
    sampleRate  = newSampleRate;
    bpm         = newBpm;
    beatsPerBar = juce::jmax (1, newBeatsPerBar);
    beatUnit    = newBeatUnit > 0 ? newBeatUnit : 4;
    // 0/1 = beats only; 2/3/4 subdivisions per beat.
    subdivision = juce::jmax (1, normaliseSubdivision (newSubdivision));
    accentMask  = newAccentMask;
}

void MetronomePlayer::reset()
{
    activeClicks.clear();
    lastStartPos = 0;
}

int MetronomePlayer::normaliseSubdivision (int subdivision) noexcept
{
    return (subdivision == 2 || subdivision == 3 || subdivision == 4) ? subdivision : 0;
}

uint16_t MetronomePlayer::fitAccentMaskToBeats (uint16_t mask, int beats) noexcept
{
    const int n = juce::jmax (1, beats);
    const uint16_t limit = static_cast<uint16_t> ((n >= 16) ? 0xFFFFu : ((1u << n) - 1u));
    return static_cast<uint16_t> (mask & limit);
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
                              const std::shared_ptr<const std::vector<float>>& accent,
                              const std::shared_ptr<const std::vector<float>>& sub)
{
    // Copy the shared_ptrs once per call so a message-thread resynth can
    // never invalidate the buffers mid-render.
    const auto normalClick = tick;
    const auto accentClick = accent;
    const auto subClick    = sub;

    if (bpm <= 0.0 || sampleRate <= 0.0)
        return;

    // BPM is quarter-note referenced; the shared grid helper derives the beat
    // from the denominator.
    const double samplesPerBeat = LooperGrid::samplesPerBeat (sampleRate, static_cast<float> (bpm), beatUnit);
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
     && (accentClick == nullptr || accentClick->empty())
     && (subClick == nullptr || subClick->empty()))
        return;

    // Schedule on a subdivision grid: grid index g lands at
    // g * (samplesPerBeat / subdivision). An index that is a whole beat
    // (g % subdivision == 0) gets the tick or, per the accent mask, the
    // accent; the in-between indices get the softer sub-click (never
    // accented). Walking the grid — rather than only the integer beats —
    // also catches a beat's accent or sub-clicks when they land in a later
    // block than the beat itself.
    const int subdiv = juce::jmax (1, subdivision);
    const double samplesPerClick = samplesPerBeat / subdiv;
    if (samplesPerClick <= 0.0)
        return;

    const bool haveTick   = normalClick != nullptr && ! normalClick->empty();
    const bool haveAccent = accentClick != nullptr && ! accentClick->empty();
    const bool haveSub    = subClick != nullptr && ! subClick->empty();
    const int  barLength  = juce::jmax (1, beatsPerBar);

    const juce::int64 firstGrid = static_cast<juce::int64> (std::ceil (static_cast<double> (startPos) / samplesPerClick));
    const juce::int64 lastGrid  = static_cast<juce::int64> (std::floor (static_cast<double> (endPos) / samplesPerClick));

    for (juce::int64 grid = firstGrid; grid <= lastGrid; ++grid)
    {
        const juce::int64 clickSample = static_cast<juce::int64> (grid * samplesPerClick);
        if (clickSample < startPos || clickSample >= endPos)
            continue;

        const bool isBeat = (grid % subdiv) == 0;

        std::shared_ptr<const std::vector<float>> click;
        if (! isBeat)
        {
            if (! haveSub)
                continue;
            click = subClick;
        }
        else
        {
            const juce::int64 beat = grid / subdiv;
            const bool accented = haveAccent
                && ((accentMask >> (beat % barLength)) & 1) != 0;
            if (accented)
                click = accentClick;
            else if (haveTick)
                click = normalClick;
            else
                continue;
        }

        ActiveClick ac;
        ac.buffer     = click;
        ac.nextSample = clickSample;
        ac.readPos    = 0;
        renderTail (buffer, ac, startPos, endPos, numChannels);
        activeClicks.push_back (ac);
    }
}
