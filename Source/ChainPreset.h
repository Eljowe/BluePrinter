#pragma once

#include <JuceHeader.h>

// Pure helpers for named chain presets (0033). A preset is a small JSON
// document holding a chain's *rig* — plugin paths/order/bypass/state blobs
// plus the chain's output volume/mute and MIDI toggle/channel filter — and
// deliberately NOT its id, name, input mask or record-on-capture flag
// (those are project wiring, not the rig). One document per file in the
// chain-presets folder.
//
// Kept free of the plugin host and the filesystem so it is unit-testable
// (Tests/test_ChainPreset.cpp); the processor/editor own the file I/O and
// the deferred load.
namespace ChainPreset
{
    // Bump when the on-disk shape changes. A document whose version is not
    // currentVersion is refused rather than half-applied.
    inline constexpr int currentVersion = 1;

    // The document's recognised top-level fields. Anything else is a warning
    // (forward compatibility), never a silent partial apply.
    juce::StringArray knownKeys();

    // Build a preset document from a chain's full getChainState() var,
    // keeping only the rig fields and adding { version, name }.
    juce::var makeDocument (const juce::String& presetName, const juce::var& chainState);

    struct Validation
    {
        bool ok = false;
        int  version = 0;
        juce::String error;    // fatal: do not load
        juce::String warning;  // non-fatal: load, but tell the user
    };

    // Validate a parsed document. A wrong/missing version is fatal; unknown
    // top-level keys produce a warning.
    Validation validateDocument (const juce::var& document);

    // The rig payload to feed a chain (the document minus version/name), or
    // a void var when the document is not an object.
    juce::var getPayload (const juce::var& document);

    // The preset's user-facing name, or an empty string.
    juce::String getPresetName (const juce::var& document);

    // A safe file stem for the preset (no path separators or reserved
    // characters, no trailing dots, not a Windows device name). Falls back
    // to "preset" when nothing usable remains; capped at 120 chars.
    juce::String sanitisePresetFileName (const juce::String& presetName);
}
