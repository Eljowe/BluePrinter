#include "MelodyPlayer.h"

#include <algorithm>
#include <cmath>

void MelodyPlayer::prepare (double newSampleRate)
{
    if (newSampleRate > 0.0)
        sampleRate = newSampleRate;
}

void MelodyPlayer::setNotes (Notes newNotes)
{
    playing.store (false, std::memory_order_release);
    position.store (0, std::memory_order_release);

    int64_t end = 0;
    for (const auto& note : newNotes)
        end = std::max (end, note.startSample + note.lengthSamples);

    length.store (end, std::memory_order_release);
    notes = std::make_shared<const Notes> (std::move (newNotes));
}

void MelodyPlayer::clearNotes()
{
    playing.store (false, std::memory_order_release);
    position.store (0, std::memory_order_release);
    length.store (0, std::memory_order_release);
    notes.reset();
}

void MelodyPlayer::start (int64_t fromSample)
{
    if (notes == nullptr || notes->empty())
        return;

    const int64_t end = length.load (std::memory_order_acquire);
    if (fromSample < 0 || fromSample >= end)
        fromSample = 0;   // past the end (or unset) restarts from the top

    position.store (fromSample, std::memory_order_release);
    playing.store (true, std::memory_order_release);
}

void MelodyPlayer::stop()
{
    playing.store (false, std::memory_order_release);
    position.store (0, std::memory_order_release);
}

bool MelodyPlayer::render (juce::AudioBuffer<float>& dest, int numSamples)
{
    if (! playing.load (std::memory_order_acquire))
        return false;

    // Local strong ref: the message thread only swaps `notes` while stopped,
    // but holding a copy keeps the buffer alive for this block regardless.
    const auto snapshot = notes;
    if (snapshot == nullptr || snapshot->empty() || numSamples <= 0)
    {
        playing.store (false, std::memory_order_release);
        return false;
    }

    const int64_t startSample = position.load (std::memory_order_acquire);
    const int64_t endSample   = startSample + numSamples;
    const int64_t endOfMelody = length.load (std::memory_order_acquire);

    const int numChannels = dest.getNumChannels();
    const int numOut      = dest.getNumSamples();
    const int toWrite     = juce::jmin (numSamples, numOut);
    const double sr       = sampleRate;
    const int attack      = juce::jmax (1, static_cast<int> (attackSeconds * sr));
    const int release     = juce::jmax (1, static_cast<int> (releaseSeconds * sr));

    for (const auto& note : *snapshot)
    {
        const int64_t noteStart = note.startSample;
        const int64_t noteEnd   = note.startSample + note.lengthSamples;
        if (noteEnd <= startSample || noteStart >= endSample)
            continue;

        const double frequency = 440.0 * std::pow (2.0, (static_cast<double> (note.midi) - 69.0) / 12.0);
        const int64_t from = juce::jmax (startSample, noteStart);
        const int64_t to   = juce::jmin (endSample, noteEnd);
        const int64_t noteLength = noteEnd - noteStart;

        for (int64_t s = from; s < to; ++s)
        {
            const int outIndex = static_cast<int> (s - startSample);
            if (outIndex < 0 || outIndex >= toWrite)
                continue;

            const int64_t local = s - noteStart;
            float envelope = 1.0f;
            if (local < attack)
                envelope = static_cast<float> (local) / static_cast<float> (attack);
            else if (noteLength - local < release)
                envelope = static_cast<float> (noteLength - local) / static_cast<float> (release);
            envelope = juce::jlimit (0.0f, 1.0f, envelope);

            const float value = amplitude * envelope
                * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi
                                                * frequency * static_cast<double> (local) / sr));

            for (int ch = 0; ch < numChannels; ++ch)
                dest.addSample (ch, outIndex, value);
        }
    }

    position.store (endSample, std::memory_order_release);

    if (endSample >= endOfMelody)
    {
        playing.store (false, std::memory_order_release);
        return false;
    }

    return true;
}
