#include "TestRunner.h"
#include "ChainStateMigration.h"

namespace
{
juce::var makeObject()
{
    return juce::var (new juce::DynamicObject());
}

void set (juce::var& object, const char* key, const juce::var& value)
{
    object.getDynamicObject()->setProperty (key, value);
}
}

BP_TEST (ChainStateMigration_readsTheCurrentChainsArray)
{
    auto chain0 = makeObject();
    set (chain0, "id", "chain0");
    set (chain0, "name", "Lead");

    juce::Array<juce::var> chains;
    chains.add (chain0);
    chains.add (juce::var (42)); // non-object entries are skipped

    auto state = makeObject();
    set (state, "chains", chains);

    const auto specs = ChainStateMigration::normalise (state);
    BP_CHECK_EQ (static_cast<int> (specs.size()), 1);
    BP_CHECK (specs[0].hasState);
    BP_CHECK_EQ (specs[0].name, juce::String()); // createChain picks the name
    BP_CHECK_EQ (specs[0].errorLabel, juce::String ("Chain:"));
    BP_CHECK (specs[0].state.isObject());
    BP_CHECK_EQ (specs[0].state.getDynamicObject()->getProperty ("id").toString(),
                 juce::String ("chain0"));
}

BP_TEST (ChainStateMigration_migratesTheLegacySplitFormat)
{
    auto state = makeObject();
    set (state, "midiChain", makeObject());
    set (state, "audioChain", makeObject());

    const auto specs = ChainStateMigration::normalise (state);
    BP_CHECK_EQ (static_cast<int> (specs.size()), 2);

    BP_CHECK_EQ (specs[0].name, juce::String ("MIDI Chain"));
    BP_CHECK (specs[0].wantsMidi);
    BP_CHECK (specs[0].recordOnCapture);
    BP_CHECK (specs[0].hasState);
    BP_CHECK_EQ (specs[0].errorLabel, juce::String ("MIDI chain:"));

    BP_CHECK_EQ (specs[1].name, juce::String ("Audio FX Chain"));
    BP_CHECK (! specs[1].wantsMidi);
    BP_CHECK (specs[1].recordOnCapture);
    BP_CHECK (specs[1].hasState);
    BP_CHECK_EQ (specs[1].errorLabel, juce::String ("Audio chain:"));
}

BP_TEST (ChainStateMigration_stillCreatesAMissingSplitChain)
{
    auto state = makeObject();
    set (state, "midiChain", makeObject());

    const auto specs = ChainStateMigration::normalise (state);
    BP_CHECK_EQ (static_cast<int> (specs.size()), 2);
    BP_CHECK (specs[0].hasState);
    BP_CHECK (! specs[1].hasState);
    BP_CHECK_EQ (specs[1].name, juce::String ("Audio FX Chain"));
    BP_CHECK (! specs[1].wantsMidi);
}

BP_TEST (ChainStateMigration_migratesThePreSplitSlotsFormat)
{
    auto state = makeObject();
    set (state, "slots", juce::Array<juce::var>());

    const auto specs = ChainStateMigration::normalise (state);
    BP_CHECK_EQ (static_cast<int> (specs.size()), 1);
    BP_CHECK_EQ (specs[0].name, juce::String ("Audio FX Chain"));
    BP_CHECK (! specs[0].wantsMidi);
    BP_CHECK (specs[0].recordOnCapture);
    BP_CHECK (specs[0].hasState);
    BP_CHECK_EQ (specs[0].errorLabel, juce::String ("Audio chain:"));
}

BP_TEST (ChainStateMigration_detectDedupeFixesMissingAndDuplicateIds)
{
    const std::vector<juce::String> raw { "", "a", "a", "b", "chain3", "chain3" };
    int nextChainId = 1;

    const auto fixed = ChainStateMigration::dedupeIds (raw, nextChainId);
    BP_CHECK_EQ (static_cast<int> (fixed.size()), 6);
    BP_CHECK_EQ (fixed[0], juce::String ("chain4"));
    BP_CHECK_EQ (fixed[1], juce::String ("a"));
    BP_CHECK_EQ (fixed[2], juce::String ("chain5"));
    BP_CHECK_EQ (fixed[3], juce::String ("b"));
    BP_CHECK_EQ (fixed[4], juce::String ("chain3"));
    BP_CHECK_EQ (fixed[5], juce::String ("chain6"));
    BP_CHECK_EQ (nextChainId, 7);
}

BP_TEST (ChainStateMigration_dedupeRaisesTheIdCounterAboveExistingIds)
{
    const std::vector<juce::String> raw { "chain9" };
    int nextChainId = 2;

    const auto fixed = ChainStateMigration::dedupeIds (raw, nextChainId);
    BP_CHECK_EQ (fixed[0], juce::String ("chain9"));
    BP_CHECK_EQ (nextChainId, 10);
}
