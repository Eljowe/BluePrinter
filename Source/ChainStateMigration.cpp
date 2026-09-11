#include "ChainStateMigration.h"

#include <set>

namespace ChainStateMigration
{
std::vector<Spec> normalise (const juce::var& state)
{
    std::vector<Spec> specs;

    auto* obj = state.getDynamicObject();
    if (obj == nullptr)
        return specs;

    // Current format: an explicit chains array. Each object becomes one
    // chain; per-chain routing (including inputs) is read by
    // setChainState from the object itself, so the spec only carries the
    // state value and the error prefix.
    if (obj->hasProperty ("chains"))
    {
        if (auto* arr = obj->getProperty ("chains").getArray())
        {
            for (const auto& chainVar : *arr)
            {
                if (! chainVar.isObject())
                    continue;

                Spec spec;
                spec.hasState   = true;
                spec.state      = chainVar;
                spec.errorLabel = "Chain:";
                specs.push_back (std::move (spec));
            }
        }

        return specs;
    }

    // Legacy split format: a MIDI chain and an audio FX chain. Both take
    // the full stereo input and record; the MIDI chain sees the keyboard
    // (wantsMidi true) and the audio chain does not — the same defaults
    // the old code applied.
    const bool split = obj->hasProperty ("midiChain") || obj->hasProperty ("audioChain");
    if (split)
    {
        Spec midi;
        midi.name            = "MIDI Chain";
        midi.wantsMidi       = true;
        midi.recordOnCapture = true;
        midi.errorLabel      = "MIDI chain:";
        const auto midiVar = obj->getProperty ("midiChain");
        if (midiVar.isObject())
        {
            midi.hasState = true;
            midi.state    = midiVar;
        }
        specs.push_back (std::move (midi));

        Spec audio;
        audio.name            = "Audio FX Chain";
        audio.wantsMidi       = false;
        audio.recordOnCapture = true;
        audio.errorLabel      = "Audio chain:";
        const auto audioVar = obj->getProperty ("audioChain");
        if (audioVar.isObject())
        {
            audio.hasState = true;
            audio.state    = audioVar;
        }
        specs.push_back (std::move (audio));

        return specs;
    }

    // Oldest format: a single top-level "slots" array holding the old
    // guitar FX chain. No MIDI (matching the old code).
    Spec legacy;
    legacy.name            = "Audio FX Chain";
    legacy.wantsMidi       = false;
    legacy.recordOnCapture = true;
    legacy.hasState        = true;
    legacy.state           = obj->getProperty ("slots");
    legacy.errorLabel      = "Audio chain:";
    specs.push_back (std::move (legacy));

    return specs;
}

std::vector<juce::String> dedupeIds (const std::vector<juce::String>& rawIds,
                                     int& nextChainId)
{
    std::vector<juce::String> result = rawIds;

    // Find the highest numeric "chainN" already in use, so regenerated
    // ids can never collide with an existing one.
    int maxId = -1;
    for (const auto& id : rawIds)
    {
        if (id.startsWith ("chain") && id.length() > 5)
        {
            const juce::String suffix = id.substring (5);
            const int numeric = suffix.getIntValue();
            if (suffix == juce::String (numeric))
                maxId = juce::jmax (maxId, numeric);
        }
    }

    int next = juce::jmax (maxId + 1, nextChainId);

    std::set<juce::String> seen;
    for (auto& id : result)
    {
        if (id.isEmpty() || seen.count (id) > 0)
            id = juce::String ("chain") + juce::String (next++);
        else
            seen.insert (id);
    }

    nextChainId = next;
    return result;
}
}
