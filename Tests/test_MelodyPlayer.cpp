#include "TestRunner.h"
#include "MelodyPlayer.h"

#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;

MelodyAnalyzer::NoteEvent makeNote (int64_t start, int64_t length, int midi)
{
    MelodyAnalyzer::NoteEvent note;
    note.startSample   = start;
    note.lengthSamples = length;
    note.midi          = midi;
    note.confidence    = 1.0f;
    return note;
}

float peakOf (const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    return peak;
}
} // namespace

BP_TEST (MelodyPlayer_doesNotRenderWhenIdle)
{
    MelodyPlayer player;
    player.prepare (kSampleRate);
    player.setNotes ({ makeNote (0, 4800, 69) });

    juce::AudioBuffer<float> dest (2, 480);
    dest.clear();

    BP_CHECK (! player.render (dest, 480));
    BP_CHECK_EQ (peakOf (dest), 0.0f);
    BP_CHECK (! player.isPlaying());
}

BP_TEST (MelodyPlayer_rendersAndAdvances)
{
    MelodyPlayer player;
    player.prepare (kSampleRate);
    player.setNotes ({ makeNote (0, 4800, 69) });

    player.start();
    BP_CHECK (player.isPlaying());

    juce::AudioBuffer<float> dest (1, 480);
    dest.clear();
    BP_CHECK (player.render (dest, 480));
    BP_CHECK (peakOf (dest) > 0.0f);
    BP_CHECK_EQ (player.getPosition(), static_cast<int64_t> (480));
}

BP_TEST (MelodyPlayer_silentBeforeTheFirstNote)
{
    MelodyPlayer player;
    player.prepare (kSampleRate);
    player.setNotes ({ makeNote (4800, 4800, 69) });

    player.start();

    juce::AudioBuffer<float> lead (1, 480);
    lead.clear();
    player.render (lead, 480);
    BP_CHECK_EQ (peakOf (lead), 0.0f);

    juce::AudioBuffer<float> note (1, 4800);
    note.clear();
    player.render (note, 4800);
    BP_CHECK (peakOf (note) > 0.0f);
}

BP_TEST (MelodyPlayer_stopsAtTheEndOfTheMelody)
{
    MelodyPlayer player;
    player.prepare (kSampleRate);
    player.setNotes ({ makeNote (0, 4800, 69) });

    player.start();

    juce::AudioBuffer<float> dest (1, 4800);
    dest.clear();
    BP_CHECK (! player.render (dest, 4800));
    BP_CHECK (! player.isPlaying());
    BP_CHECK_EQ (player.getLength(), static_cast<int64_t> (4800));
}

BP_TEST (MelodyPlayer_addsToExistingOutput)
{
    MelodyPlayer player;
    player.prepare (kSampleRate);
    player.setNotes ({ makeNote (0, 4800, 69) });

    player.start();

    juce::AudioBuffer<float> dest (1, 2400);
    for (int i = 0; i < dest.getNumSamples(); ++i)
        dest.setSample (0, i, 0.5f);

    player.render (dest, 2400);
    BP_CHECK (peakOf (dest) > 0.55f);
}

BP_TEST (MelodyPlayer_wontStartWithoutNotes)
{
    MelodyPlayer player;
    player.prepare (kSampleRate);
    player.start();
    BP_CHECK (! player.isPlaying());

    juce::AudioBuffer<float> dest (1, 64);
    dest.clear();
    BP_CHECK (! player.render (dest, 64));
}
