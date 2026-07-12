#include "KeyDetector.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
// Krumhansl-Schmuckler key profiles, taken from Krumhansl & Schmuckler
// (1986). The major profile peaks on the tonic (index 0 of the rotated
// profile) and the dominant (index 7); the minor profile peaks on the
// tonic and the minor third. These are the canonical reference profiles
// used by virtually every chroma-based key estimator.
constexpr double kMajorProfile[12] = {
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
    2.52, 5.19, 2.39, 3.66, 2.29, 2.88
};
constexpr double kMinorProfile[12] = {
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
    2.54, 4.75, 3.98, 2.69, 3.34, 3.17
};

constexpr const char* kPitchClassNames[12] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

// Minimum correlation we'll accept as a real key. Krumhansl-Schmuckler
// on tonal music usually lands in the 0.65..0.95 range; percussion
// and atonal material sits below 0.5, so this threshold keeps junk out
// of the UI without being so strict that borderline cases get dropped.
constexpr double kMinConfidence = 0.5;

// FFT size: 4096 samples (~93 ms at 44.1 kHz). At lower sample rates
// the frequency resolution is coarser but still good enough for chroma
// estimation, which only needs to land each bin on the right pitch
// class, not the right octave.
constexpr int kFftOrder = 12;
constexpr int kFftSize  = 1 << kFftOrder;
constexpr int kHopSize  = 1024;

// Frequency range we fold into the chroma vector. Below 80 Hz the bins
// are too wide to assign to a pitch class reliably; above 5 kHz the
// energy is mostly overtones and noise that pollutes the estimate.
constexpr float kMinFreq = 80.0f;
constexpr float kMaxFreq = 5000.0f;

void applyHannWindow (float* data, int n)
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    for (int i = 0; i < n; ++i)
        data[i] *= 0.5f - 0.5f * std::cos (twoPi * i / static_cast<float> (n - 1));
}

double pearsonCorrelation (const double* x, const double* y, int n)
{
    double sumX = 0.0, sumY = 0.0;
    for (int i = 0; i < n; ++i)
    {
        sumX += x[i];
        sumY += y[i];
    }
    const double meanX = sumX / n;
    const double meanY = sumY / n;
    double num = 0.0, denomX = 0.0, denomY = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double dx = x[i] - meanX;
        const double dy = y[i] - meanY;
        num    += dx * dy;
        denomX += dx * dx;
        denomY += dy * dy;
    }
    const double denom = std::sqrt (denomX * denomY);
    return denom > 0.0 ? num / denom : 0.0;
}
} // namespace

KeyDetectionResult KeyDetector::detectKey (const juce::AudioBuffer<float>& audio,
                                            double sampleRate)
{
    KeyDetectionResult result;

    if (audio.getNumSamples() < kFftSize || audio.getNumChannels() <= 0 || sampleRate <= 0.0)
        return result;

    // Mix down to mono. Key estimation works on the pitch-class content
    // of the signal, not on stereo placement, so a simple average is
    // fine and avoids channel-balancing artefacts biasing the chroma.
    const int numSamples  = audio.getNumSamples();
    const int numChannels = audio.getNumChannels();
    std::vector<float> mono (static_cast<size_t> (numSamples), 0.0f);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* src = audio.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t> (i)] += src[i];
    }
    const float invCh = 1.0f / static_cast<float> (numChannels);
    for (auto& s : mono) s *= invCh;

    juce::dsp::FFT fft (kFftOrder);
    std::vector<float> frame (static_cast<size_t> (kFftSize));
    std::vector<float> fftData (static_cast<size_t> (kFftSize) * 2, 0.0f);

    double chroma[12] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    int frames = 0;

    for (int offset = 0; offset + kFftSize <= numSamples; offset += kHopSize)
    {
        for (int i = 0; i < kFftSize; ++i)
            frame[static_cast<size_t> (i)] = mono[static_cast<size_t> (offset + i)];

        applyHannWindow (frame.data(), kFftSize);

        // juce::dsp::FFT uses a packed real-only layout: the first
        // fftSize floats hold the real input, the second fftSize floats
        // are scratch for the complex output. Zero the imaginary half
        // before each transform.
        std::copy (frame.begin(), frame.end(), fftData.begin());
        std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);
        fft.performRealOnlyForwardTransform (fftData.data());

        for (int bin = 1; bin < kFftSize / 2; ++bin)
        {
            const float real = fftData[static_cast<size_t> (bin * 2)];
            const float imag = fftData[static_cast<size_t> (bin * 2 + 1)];
            const float magnitude = std::sqrt (real * real + imag * imag);

            const float freq = static_cast<float> (bin) * static_cast<float> (sampleRate) / static_cast<float> (kFftSize);
            if (freq < kMinFreq || freq > kMaxFreq)
                continue;

            // Map bin frequency to a MIDI note, then take the pitch
            // class. The rounding is what makes this a "chroma"
            // feature — every octave folds to the same bin.
            const double midi = 69.0 + 12.0 * std::log2 (static_cast<double> (freq) / 440.0);
            const int midiRound = static_cast<int> (std::lround (midi));
            const int pitchClass = ((midiRound % 12) + 12) % 12;
            chroma[pitchClass] += magnitude;
        }
        ++frames;
    }

    if (frames == 0)
        return result;

    // Normalise so the correlation is scale-invariant. We could also
    // normalise the key profiles, but the published numbers above are
    // already on a comparable scale.
    double chromaSum = 0.0;
    for (double c : chroma) chromaSum += c;
    if (chromaSum <= 0.0)
        return result;
    for (double& c : chroma) c /= chromaSum;

    // Score against all 24 keys. For each tonic we rotate the
    // published profile so that index 0 lands on the tonic, then take
    // the Pearson correlation with the chroma vector.
    double bestCorrelation = -2.0;
    int    bestTonic       = 0;
    bool   bestIsMinor     = false;

    double profile[12];
    for (int tonic = 0; tonic < 12; ++tonic)
    {
        for (int i = 0; i < 12; ++i)
            profile[i] = kMajorProfile[(i - tonic + 12) % 12];
        const double majorCorr = pearsonCorrelation (chroma, profile, 12);
        if (majorCorr > bestCorrelation)
        {
            bestCorrelation = majorCorr;
            bestTonic       = tonic;
            bestIsMinor     = false;
        }

        for (int i = 0; i < 12; ++i)
            profile[i] = kMinorProfile[(i - tonic + 12) % 12];
        const double minorCorr = pearsonCorrelation (chroma, profile, 12);
        if (minorCorr > bestCorrelation)
        {
            bestCorrelation = minorCorr;
            bestTonic       = tonic;
            bestIsMinor     = true;
        }
    }

    if (bestCorrelation < kMinConfidence)
        return result;

    result.key        = juce::String (kPitchClassNames[bestTonic]) + (bestIsMinor ? " minor" : " major");
    result.confidence = static_cast<float> (bestCorrelation);

    // Derive a "used notes" list from the same chroma vector. We keep
    // bins that are both above an absolute floor (so quiet material with
    // a real key doesn't list every noise bin) and within a fraction of
    // the strongest bin (so a single loud note doesn't drown the rest).
    // Sorted strongest-first so the UI can just join with commas.
    constexpr double kNoteAbsoluteFloor = 0.04; // 4% of total chroma energy
    constexpr double kNoteRelativeFloor = 0.5;  // 50% of the strongest bin

    double maxChroma = 0.0;
    for (double c : chroma) maxChroma = juce::jmax (maxChroma, c);

    if (maxChroma > 0.0)
    {
        const double relFloor = maxChroma * kNoteRelativeFloor;
        // Indices in chroma order, tagged with magnitude, sorted desc.
        struct Bin { int pc; double mag; };
        std::vector<Bin> bins;
        bins.reserve (12);
        for (int pc = 0; pc < 12; ++pc)
        {
            if (chroma[pc] >= kNoteAbsoluteFloor && chroma[pc] >= relFloor)
                bins.push_back ({ pc, chroma[pc] });
        }
        std::sort (bins.begin(), bins.end(),
                   [](const Bin& a, const Bin& b) { return a.mag > b.mag; });

        // A snippet that's basically one note (1 bin) or noise (0 bins)
        // isn't useful to list. Keep the 2..12 range.
        if (bins.size() >= 2)
        {
            for (const auto& b : bins)
                result.detectedNotes.add (kPitchClassNames[b.pc]);
        }
    }

    return result;
}
