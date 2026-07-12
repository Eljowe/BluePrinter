#pragma once

#include <JuceHeader.h>

// Result of a musical key detection. `key` is empty when the audio is
// too short, too quiet, too atonal, or the best match falls below the
// confidence threshold. `confidence` is the Pearson correlation between
// the audio's chroma vector and the best-matching Krumhansl-Schmuckler
// key profile (roughly 0.0..1.0, typically 0.6..0.9 for tonal music).
// `detectedNotes` lists the pitch classes present in the snippet (e.g.
// {"C", "E", "G"} for a C-major triad), sorted by chroma strength
// descending. Empty when the audio has no clearly-pitched content.
struct KeyDetectionResult
{
    juce::String      key;
    float             confidence = 0.0f;
    juce::StringArray detectedNotes;
};

// Estimate the musical key of a mono or stereo buffer using a
// FFT-based chroma vector + Krumhansl-Schmuckler key profile
// correlation. The analysis is short (a few hundred ms for a 10 s
// snippet) but the function is still called off the message thread
// by the caller — see PluginProcessor::detectSnippetKey.
class KeyDetector
{
public:
    static KeyDetectionResult detectKey (const juce::AudioBuffer<float>& audio,
                                         double sampleRate);
};
