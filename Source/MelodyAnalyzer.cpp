#include "MelodyAnalyzer.h"
#include "KeyDetector.h"
#include "Tuner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
int medianOf (std::vector<int> values)
{
    std::sort (values.begin(), values.end());
    return values[values.size() / 2];
}
} // namespace

MelodyAnalyzer::Result MelodyAnalyzer::analyze (const juce::AudioBuffer<float>& audio,
                                                double sampleRate,
                                                const Settings& settings)
{
    Result result;

    const int numSamples  = audio.getNumSamples();
    const int numChannels = audio.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0 || sampleRate <= 0.0)
        return result;

    const int frameSize = juce::jmax (256, settings.frameSize);
    const int hopSize   = juce::jmax (1, juce::jmin (settings.hopSize, frameSize));
    if (numSamples < frameSize)
        return result;

    // Mix down to mono. Pitch detection works on the monophonic content of the
    // signal, not its stereo placement.
    std::vector<float> mono (static_cast<size_t> (numSamples), 0.0f);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* src = audio.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t> (i)] += src[i];
    }
    const float invCh = 1.0f / static_cast<float> (numChannels);
    for (auto& s : mono)
        s *= invCh;

    const int numFrames = 1 + (numSamples - frameSize) / hopSize;

    struct Frame
    {
        bool  voiced    = false;
        int   midi      = 0;   // rounded pitch
        float midiExact = 0.0f;
        float confidence = 0.0f;
    };

    std::vector<Frame> frames (static_cast<size_t> (numFrames));

    for (int f = 0; f < numFrames; ++f)
    {
        const int offset = f * hopSize;
        const auto reading = Tuner::detectPitch (mono.data() + offset, frameSize, sampleRate,
                                                 settings.minHz, settings.maxHz);

        auto& frame = frames[static_cast<size_t> (f)];
        if (reading.frequency > 0.0f && reading.confidence >= settings.minConfidence)
        {
            frame.voiced     = true;
            frame.confidence = reading.confidence;
            frame.midiExact  = static_cast<float> (69.0
                + 12.0 * std::log2 (static_cast<double> (reading.frequency) / 440.0));
            frame.midi       = static_cast<int> (std::lround (frame.midiExact));
        }
    }

    // Median-filter the rounded pitch over the nearby voiced frames. This
    // erases isolated octave errors and vibrato excursions without moving the
    // note's overall pitch.
    const int halfWindow = juce::jmax (1, settings.medianWindow) / 2;
    const int unvoiced   = std::numeric_limits<int>::min();
    std::vector<int> smoothed (static_cast<size_t> (numFrames), unvoiced);

    for (int f = 0; f < numFrames; ++f)
    {
        if (! frames[static_cast<size_t> (f)].voiced)
            continue;

        std::vector<int> neighbours;
        neighbours.reserve (static_cast<size_t> (2 * halfWindow + 1));
        for (int k = -halfWindow; k <= halfWindow; ++k)
        {
            const int g = f + k;
            if (g >= 0 && g < numFrames && frames[static_cast<size_t> (g)].voiced)
                neighbours.push_back (frames[static_cast<size_t> (g)].midi);
        }

        if (! neighbours.empty())
            smoothed[static_cast<size_t> (f)] = medianOf (std::move (neighbours));
    }

    // Segment consecutive voiced frames that share the smoothed pitch.
    struct Segment
    {
        int    startFrame = 0;
        int    endFrame   = 0;
        int    midi       = 0;
        double midiSum    = 0.0;
        double confSum    = 0.0;
        int    frameCount = 0;
    };

    std::vector<Segment> segments;
    int f = 0;
    while (f < numFrames)
    {
        if (! frames[static_cast<size_t> (f)].voiced)
        {
            ++f;
            continue;
        }

        Segment segment;
        segment.startFrame = f;
        segment.endFrame   = f;
        segment.midi       = smoothed[static_cast<size_t> (f)];

        while (f < numFrames
               && frames[static_cast<size_t> (f)].voiced
               && smoothed[static_cast<size_t> (f)] == segment.midi)
        {
            segment.endFrame = f;
            segment.midiSum += frames[static_cast<size_t> (f)].midiExact;
            segment.confSum += frames[static_cast<size_t> (f)].confidence;
            ++segment.frameCount;
            ++f;
        }

        segments.push_back (std::move (segment));
    }

    // Merge same-pitch segments separated by a short unvoiced gap (legato).
    std::vector<Segment> merged;
    for (auto& segment : segments)
    {
        if (! merged.empty()
            && merged.back().midi == segment.midi
            && segment.startFrame - merged.back().endFrame - 1 <= settings.maxGapFrames)
        {
            auto& previous = merged.back();
            previous.endFrame   = segment.endFrame;
            previous.midiSum   += segment.midiSum;
            previous.confSum   += segment.confSum;
            previous.frameCount += segment.frameCount;
        }
        else
        {
            merged.push_back (segment);
        }
    }

    for (const auto& segment : merged)
    {
        const int lengthFrames = segment.endFrame - segment.startFrame + 1;
        if (lengthFrames < juce::jmax (1, settings.minNoteFrames) || segment.frameCount <= 0)
            continue;

        NoteEvent note;
        note.startSample   = static_cast<int64_t> (segment.startFrame) * hopSize;
        note.lengthSamples = static_cast<int64_t> (lengthFrames) * hopSize;
        note.midi          = segment.midi;

        const double meanMidi = segment.midiSum / static_cast<double> (segment.frameCount);
        note.cents         = static_cast<float> ((meanMidi - static_cast<double> (segment.midi)) * 100.0);
        note.confidence    = static_cast<float> (segment.confSum / static_cast<double> (segment.frameCount));

        result.notes.push_back (note);
    }

    if (settings.detectKey)
    {
        const auto key = KeyDetector::detectKey (audio, sampleRate);
        result.key           = key.key;
        result.keyConfidence = key.confidence;
        result.detectedNotes = key.detectedNotes;
    }

    return result;
}
