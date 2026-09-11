#pragma once

#include <JuceHeader.h>
#include <vector>

// Pure parsing of the saved "pluginChains" bundle, extracted from
// BluePrinterAudioProcessor::applyChainState so the three supported
// shapes (and the id de-duplication) can be unit tested without a
// processor or any plugin instantiation.
//
// This module deliberately knows nothing about PluginChain /
// BluePrinterAudioProcessor: it only inspects the saved juce::var and
// describes the chains the processor should create. The processor then
// calls createChain()/setChainState() for each spec.
namespace ChainStateMigration
{
    // One chain to create during a restore.
    struct Spec
    {
        // Empty means "let createChain() generate a default name".
        juce::String name;

        bool wantsMidi       = true;
        bool recordOnCapture = true;

        // Whether `state` should be handed to setChainState(). False for
        // a legacy split entry that was missing its object entirely.
        bool hasState = false;

        // The per-chain state value passed to setChainState(): a current
        // chains[] entry, a legacy midiChain/audioChain object, or the
        // pre-split top-level "slots" array.
        juce::var state;

        // Prefix used when surfacing a restore error for this chain,
        // including the trailing colon (e.g. "MIDI chain:").
        juce::String errorLabel;
    };

    // Normalises any supported saved chain-state shape into the list of
    // chains to restore. Understands, in priority order:
    //   1. current   { "chains": [ ... ] }
    //   2. split     { "midiChain": {...}, "audioChain": {...} }
    //   3. pre-split { "slots": [ ... ] }
    // Non-object entries in a "chains" array are skipped, matching the
    // restore behaviour. Returns an empty list when `state` is not an
    // object.
    std::vector<Spec> normalise (const juce::var& state);

    // Assigns stable, unique ids to a list of raw chain ids.
    //
    // Missing or duplicate ids are replaced with "chainN", starting above
    // the highest numeric "chainN" already present, and `nextChainId` is
    // advanced past every id handed out. Unique ids that don't match the
    // "chainN" shape are kept as-is. `nextChainId` is only ever raised,
    // never lowered.
    std::vector<juce::String> dedupeIds (const std::vector<juce::String>& rawIds,
                                         int& nextChainId);
}
