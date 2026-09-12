#include "TestRunner.h"
#include "ChainPreset.h"

namespace
{
juce::var makeChainState()
{
    auto* chain = new juce::DynamicObject();
    chain->setProperty ("id", "chain3");
    chain->setProperty ("name", "Fuzz lead");
    chain->setProperty ("inputs", juce::Array<juce::var> { 0, 1 });
    chain->setProperty ("recordOnCapture", true);
    chain->setProperty ("volume", -3.0);
    chain->setProperty ("muted", true);
    chain->setProperty ("monitorSolo", true);
    chain->setProperty ("monitorMuted", true);
    chain->setProperty ("wantsMidi", false);
    chain->setProperty ("midiChannels", juce::Array<juce::var> { 1, 2 });
    chain->setProperty ("pending", 0);

    auto* slot = new juce::DynamicObject();
    slot->setProperty ("path", "C:\\VST3\\Amp.vst3");
    slot->setProperty ("bypassed", true);
    slot->setProperty ("name", "Amp");
    slot->setProperty ("state", "QUJD");
    juce::Array<juce::var> slots;
    slots.add (juce::var (slot));
    chain->setProperty ("slots", slots);

    return juce::var (chain);
}
}

BP_TEST (ChainPreset_makeDocumentKeepsRigDropsWiring)
{
    const auto doc = ChainPreset::makeDocument ("Clean amp", makeChainState());
    auto* obj = doc.getDynamicObject();
    BP_CHECK (obj != nullptr);

    BP_CHECK_EQ (static_cast<int> (obj->getProperty ("version")), ChainPreset::currentVersion);
    BP_CHECK_EQ (obj->getProperty ("name").toString(), juce::String ("Clean amp"));

    BP_CHECK (obj->hasProperty ("slots"));
    BP_CHECK (obj->hasProperty ("volume"));
    BP_CHECK (obj->hasProperty ("muted"));
    BP_CHECK (obj->hasProperty ("wantsMidi"));
    BP_CHECK (obj->hasProperty ("midiChannels"));

    // Identity / project wiring is dropped.
    BP_CHECK (! obj->hasProperty ("id"));
    BP_CHECK (! obj->hasProperty ("inputs"));
    BP_CHECK (! obj->hasProperty ("recordOnCapture"));
    BP_CHECK (! obj->hasProperty ("monitorSolo"));
    BP_CHECK (! obj->hasProperty ("monitorMuted"));
    BP_CHECK (! obj->hasProperty ("pending"));

    BP_CHECK_EQ (obj->getProperty ("slots").size(), 1);
    BP_CHECK_EQ (obj->getProperty ("slots")[0].getProperty ("name", juce::var()).toString(),
                 juce::String ("Amp"));
}

BP_TEST (ChainPreset_validateAcceptsCurrentVersion)
{
    const auto doc = ChainPreset::makeDocument ("Clean amp", makeChainState());
    const auto v = ChainPreset::validateDocument (doc);
    BP_CHECK (v.ok);
    BP_CHECK_EQ (v.version, ChainPreset::currentVersion);
    BP_CHECK (v.error.isEmpty());
    BP_CHECK (v.warning.isEmpty());
}

BP_TEST (ChainPreset_validateRejectsWrongOrMissingVersion)
{
    auto* old = new juce::DynamicObject();
    old->setProperty ("version", 999);
    const auto wrong = ChainPreset::validateDocument (juce::var (old));
    BP_CHECK (! wrong.ok);
    BP_CHECK (wrong.error.isNotEmpty());

    auto* noVersion = new juce::DynamicObject();
    noVersion->setProperty ("name", "x");
    const auto missing = ChainPreset::validateDocument (juce::var (noVersion));
    BP_CHECK (! missing.ok);
    BP_CHECK (missing.error.isNotEmpty());

    juce::var notObject (42);
    BP_CHECK (! ChainPreset::validateDocument (notObject).ok);
}

BP_TEST (ChainPreset_validateWarnsOnUnknownFields)
{
    const auto doc = ChainPreset::makeDocument ("Clean amp", makeChainState());
    doc.getDynamicObject()->setProperty ("futureThing", 7);
    const auto v = ChainPreset::validateDocument (doc);
    BP_CHECK (v.ok);
    BP_CHECK (v.warning.isNotEmpty());
}

BP_TEST (ChainPreset_getPayloadDropsVersionAndName)
{
    const auto doc = ChainPreset::makeDocument ("Clean amp", makeChainState());
    const auto payload = ChainPreset::getPayload (doc);
    auto* obj = payload.getDynamicObject();
    BP_CHECK (obj != nullptr);
    BP_CHECK (! obj->hasProperty ("version"));
    BP_CHECK (! obj->hasProperty ("name"));
    BP_CHECK (obj->hasProperty ("slots"));
    BP_CHECK (obj->hasProperty ("midiChannels"));
    BP_CHECK_EQ (ChainPreset::getPresetName (doc), juce::String ("Clean amp"));
    BP_CHECK (ChainPreset::getPayload (juce::var (juce::String ("nope"))).isVoid());
}

BP_TEST (ChainPreset_sanitisesFileNames)
{
    BP_CHECK_EQ (ChainPreset::sanitisePresetFileName ("Clean/amp:lead?"), juce::String ("Cleanamplead"));
    BP_CHECK_EQ (ChainPreset::sanitisePresetFileName ("   "), juce::String ("preset"));
    BP_CHECK_EQ (ChainPreset::sanitisePresetFileName ("Trailing..."), juce::String ("Trailing"));
    BP_CHECK_EQ (ChainPreset::sanitisePresetFileName ("con"), juce::String ("_con"));
    BP_CHECK_EQ (ChainPreset::sanitisePresetFileName ("NUL"), juce::String ("_NUL"));
    BP_CHECK_EQ (ChainPreset::sanitisePresetFileName ("Lead"), juce::String ("Lead"));
}
