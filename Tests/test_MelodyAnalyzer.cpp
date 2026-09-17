#include "TestRunner.h"
#include "MelodyAnalyzer.h"

#include <cmath>
#include <utility>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;

struct Tone
{
    double freq;
    double seconds;
};

// Renders a monophonic sequence: each tone is a windowed sine with a short
// fade so note boundaries don't click. `lead`/`trail` pad the whole buffer
// with silence; `gap` separates consecutive tones.
juce::AudioBuffer<float> renderMelody (const std::vector<Tone>& tones,
                                       double leadSeconds, double gapSeconds,
                                       double trailSeconds,
                                       float amplitude = 0.5f)
{
    const int lead  = static_cast<int> (leadSeconds * kSampleRate);
    const int gap   = static_cast<int> (gapSeconds * kSampleRate);
    const int trail = static_cast<int> (trailSeconds * kSampleRate);

    int total = lead + trail;
    for (size_t i = 0; i < tones.size(); ++i)
    {
        total += static_cast<int> (tones[i].seconds * kSampleRate);
        if (i + 1 < tones.size())
            total += gap;
    }

    juce::AudioBuffer<float> buffer (1, juce::jmax (1, total));
    buffer.clear();
    auto* data = buffer.getWritePointer (0);

    const int fade = static_cast<int> (0.005 * kSampleRate);
    int pos = lead;

    for (const auto& tone : tones)
    {
        const int n = static_cast<int> (tone.seconds * kSampleRate);
        for (int j = 0; j < n; ++j)
        {
            float env = 1.0f;
            if (j < fade)          env = static_cast<float> (j) / static_cast<float> (fade);
            else if (j > n - fade) env = static_cast<float> (n - j) / static_cast<float> (fade);

            data[pos + j] = amplitude * env
                * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi
                                                * tone.freq * static_cast<double> (pos + j)
                                                / kSampleRate));
        }
        pos += n + gap;
    }

    return buffer;
}

constexpr double kC4 = 261.6256;
constexpr double kE4 = 329.6276;
constexpr double kG4 = 391.9954;
} // namespace

BP_TEST (MelodyAnalyzer_extractsNoteSequence)
{
    const auto audio = renderMelody ({ { kC4, 0.4 }, { kE4, 0.4 }, { kG4, 0.4 } },
                                     0.0, 0.08, 0.0);

    const auto result = MelodyAnalyzer::analyze (audio, kSampleRate);

    BP_CHECK_EQ (result.notes.size(), static_cast<size_t> (3));
    if (result.notes.size() == 3)
    {
        BP_CHECK_EQ (result.notes[0].midi, 60);
        BP_CHECK_EQ (result.notes[1].midi, 64);
        BP_CHECK_EQ (result.notes[2].midi, 67);
        BP_CHECK (result.notes[0].startSample < result.notes[1].startSample);
        BP_CHECK (result.notes[1].startSample < result.notes[2].startSample);
        for (const auto& note : result.notes)
            BP_CHECK (note.lengthSamples > 0);
    }
}

BP_TEST (MelodyAnalyzer_detectsLowAndHighNotes)
{
    const auto audio = renderMelody ({ { 82.4069, 0.5 }, { 440.0, 0.4 } }, 0.0, 0.1, 0.0);

    const auto result = MelodyAnalyzer::analyze (audio, kSampleRate);

    BP_CHECK_EQ (result.notes.size(), static_cast<size_t> (2));
    if (result.notes.size() == 2)
    {
        BP_CHECK_EQ (result.notes[0].midi, 40); // E2
        BP_CHECK_EQ (result.notes[1].midi, 69); // A4
    }
}

BP_TEST (MelodyAnalyzer_ignoresSurroundingSilence)
{
    const auto audio = renderMelody ({ { kC4, 0.4 } }, 0.3, 0.0, 0.3);

    const auto result = MelodyAnalyzer::analyze (audio, kSampleRate);

    BP_CHECK_EQ (result.notes.size(), static_cast<size_t> (1));
    if (result.notes.size() == 1)
    {
        BP_CHECK_EQ (result.notes[0].midi, 60);
        // The first note starts after the lead silence, not at sample 0.
        BP_CHECK (result.notes[0].startSample > static_cast<int64_t> (0.2 * kSampleRate));
        BP_CHECK (result.notes[0].startSample < static_cast<int64_t> (0.35 * kSampleRate));
    }
}

BP_TEST (MelodyAnalyzer_dropsBriefBlips)
{
    // A 20 ms E blip inside a sustained C: windows straddle it, so the
    // median filter + minimum-note-length gate are what must reject it.
    const auto audio = renderMelody ({ { kC4, 0.4 }, { kE4, 0.02 }, { kC4, 0.4 } },
                                     0.0, 0.0, 0.0);

    const auto result = MelodyAnalyzer::analyze (audio, kSampleRate);

    BP_CHECK (! result.notes.empty());
    for (const auto& note : result.notes)
        BP_CHECK_EQ (note.midi, 60);
}

BP_TEST (MelodyAnalyzer_silenceHasNoNotes)
{
    juce::AudioBuffer<float> audio (1, static_cast<int> (kSampleRate * 2.0));
    audio.clear();

    const auto result = MelodyAnalyzer::analyze (audio, kSampleRate);

    BP_CHECK (result.notes.empty());
    BP_CHECK (result.key.isEmpty());
}

BP_TEST (MelodyAnalyzer_tooShortAudioReturnsEmpty)
{
    juce::AudioBuffer<float> audio (1, 512);
    audio.clear();

    const auto result = MelodyAnalyzer::analyze (audio, kSampleRate);

    BP_CHECK (result.notes.empty());
}

BP_TEST (MelodyAnalyzer_reportsKeyAndNotes)
{
    const auto audio = renderMelody ({ { 261.6256, 0.25 }, { 293.6648, 0.25 },
                                       { 329.6276, 0.25 }, { 349.2282, 0.25 },
                                       { 391.9954, 0.25 }, { 440.0, 0.25 },
                                       { 493.8833, 0.25 }, { 523.2511, 0.25 } },
                                     0.0, 0.03, 0.0);

    const auto result = MelodyAnalyzer::analyze (audio, kSampleRate);

    BP_CHECK_EQ (result.notes.size(), static_cast<size_t> (8));
    BP_CHECK (result.key.isNotEmpty());
    BP_CHECK (result.key.startsWith ("C "));
    BP_CHECK (! result.detectedNotes.isEmpty());
    BP_CHECK (result.detectedNotes.contains ("C"));
}

BP_TEST (MelodyAnalyzer_canSkipKeyDetection)
{
    const auto audio = renderMelody ({ { kC4, 0.4 } }, 0.0, 0.0, 0.0);

    MelodyAnalyzer::Settings settings;
    settings.detectKey = false;
    const auto result = MelodyAnalyzer::analyze (audio, kSampleRate, settings);

    BP_CHECK (! result.notes.empty());
    BP_CHECK (result.key.isEmpty());
}
