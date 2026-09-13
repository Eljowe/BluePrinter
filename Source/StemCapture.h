#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>
#include <vector>

// Per-chain stem capture (0038).
//
// During a fresh take or loop capture the processor writes the dry
// pass-through and each record-on-capture chain's post-volume output into
// these preallocated, sample-aligned buffers. `exportStems` then writes one
// file per stem, so the stems sum back to the captured mix except for the
// master monitor Output (which is never captured). Stems follow the capture
// bus only: monitor solo / monitor-mute never change them.
//
// Threading: arm() / finalise() / clear() / release() run on the message
// thread and own all allocation. writeStem() runs on the audio thread and
// only copies into the preallocated buffers — no locks, no allocation. The
// caller must disarm the capture before clear()/release() (the processor
// calls finalise() first, which sets `armed` false). Buffers are kept for
// reuse across captures; the destructor frees them after audio has stopped.
//
// Verified by Tests/test_StemCapture.cpp.
class StemCapture
{
public:
    struct Stem
    {
        juce::String name;
        juce::AudioBuffer<float> buffer;
    };

    // Bounds the total stem memory; arm() refuses to exceed it.
    static constexpr size_t maxBytes = 256u * 1024u * 1024u;

    // Message thread. Allocates a dry stem (index 0) plus one per chain
    // name, all `numChannels` wide and `maxSamples` long, zeroed. Returns
    // false (and arms nothing) when `maxSamples <= 0` or the set would
    // exceed maxBytes.
    bool arm (const juce::String& newSource,
              const std::vector<juce::String>& chainNames,
              int numChannels, int64_t maxSamples);

    // Message thread. Capture ended: fix the export length. Buffers start
    // zeroed, so a shorter capture is implicitly zero-padded to `newLength`;
    // a longer one is truncated. Also disarms.
    void finalise (int64_t newLength);

    // Message thread. Disarm and forget the length, keeping the buffers
    // allocated for the next arm().
    void clear();

    // Message thread. Free the buffers (no capture may be running).
    void release();

    // Any thread.
    bool    isArmed()     const { return armed.load (std::memory_order_acquire); }
    bool    hasStems()    const { return length.load (std::memory_order_acquire) > 0; }
    int     getNumStems() const { return static_cast<int> (stems.size()); }
    int64_t getLength()   const { return length.load (std::memory_order_acquire); }

    const juce::String& getSource() const { return source; }

    // Message thread (use while no capture is writing).
    const Stem& getStem (int index) const { return stems[static_cast<size_t> (index)]; }

    // Audio thread. Copies `src` into stem `index` at `pos`, scaled by
    // `gain`, clamped to the buffer capacity.
    void writeStem (int index, const juce::AudioBuffer<float>& src,
                    int64_t pos, float gain, int numSamples);

private:
    juce::String source;
    std::vector<Stem> stems;
    std::atomic<bool>    armed  { false };
    std::atomic<int64_t> length { 0 };
    int64_t capacity = 0;
};
