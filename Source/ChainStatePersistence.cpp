#include "ChainStatePersistence.h"

#include "ChainStateMigration.h"

ChainStatePersistence::ChainStatePersistence (Deps depsToUse)
    : deps (std::move (depsToUse))
{
}

// Build the combined plugin-chain bundle that gets written to host
// state and the standalone properties file. Holds every chain's slots
// and routing config, plus the folder-wide blocklist and cached scan
// result on the shared library, and the chain-id counter so ids stay
// stable across restores.
//
// Format:
//   {
//     "chains": [
//       { "id": "chain0", "name": "...", "inputs": [0, 1],
//         "recordOnCapture": true, "wantsMidi": true, "slots": [...] },
//       ...
//     ],
//     "nextChainId":       5,
//     "blocklist":         ["...\\Foo.vst3", ...],
//     "availablePlugins":  [{ "name": ..., "path": ..., ... }, ...]
//   }
juce::var ChainStatePersistence::makeState() const
{
    auto* obj = new juce::DynamicObject();

    juce::Array<juce::var> chainsArray;
    {
        const juce::ScopedLock sl (deps.chainLock);
        for (const auto& chain : deps.chains)
            chainsArray.add (chain->getChainState());
    }
    obj->setProperty ("chains", chainsArray);
    obj->setProperty ("nextChainId", deps.nextChainId);

    {
        juce::Array<juce::var> blocklistArray;
        for (const auto& path : deps.vst3.getBlocklist())
            blocklistArray.add (path);
        obj->setProperty ("blocklist", blocklistArray);
    }

    const auto available = deps.vst3.getAvailablePlugins();
    if (! available.isVoid())
        obj->setProperty ("availablePlugins", available);

    return juce::var (obj);
}

// Inverse of makeState. Accepts three formats:
//
//   1. The current chains-array format ("chains": [...]) — restored
//      verbatim, with ids validated by ensureUniqueChainIds.
//   2. The midiChain/audioChain-keyed split format — migrated to two
//      chains preserving each chain's slots and MIDI toggle.
//   3. The pre-split format with a single top-level "slots" array —
//      migrated to one chain holding the old guitar FX.
//
// Appends a (possibly empty) human-readable error string listing any
// plugins that were skipped; the caller surfaces it to the UI.
void ChainStatePersistence::applyState (const juce::var& state, juce::String& outError)
{
    auto* obj = state.getDynamicObject();
    if (obj == nullptr)
        return;

    // Blocklist first so each chain's setChainState can check it. The
    // blocklist is folder-wide, so restoring it is a single set
    // regardless of which chains the state contains.
    if (auto* blocklistVar = obj->getProperty ("blocklist").getArray())
    {
        juce::StringArray paths;
        for (const auto& v : *blocklistVar)
            paths.add (v.toString());
        deps.vst3.setBlocklist (paths);
    }

    // Cached scan result. Old saved states won't have this; in that
    // case we leave availablePlugins untouched (it'll be an empty
    // var and the UI will show no available plugins until the user
    // re-scans).
    if (obj->hasProperty ("availablePlugins"))
        deps.vst3.setAvailablePlugins (obj->getProperty ("availablePlugins"));

    clearChains();

    // Normalise the saved bundle (current / legacy-split / pre-split) into
    // the chains to restore, then create and load each one. The parsing
    // lives in ChainStateMigration so the migration rules are unit tested;
    // the processor only wires the specs to its own chains.
    const auto specs = ChainStateMigration::normalise (state);
    for (const auto& spec : specs)
    {
        auto* chain = createChain (spec.name, ChainInputBoth, spec.wantsMidi, spec.recordOnCapture);

        if (chain == nullptr)
        {
            // Past the owner's chain maximum: report it once and skip the
            // rest (the remaining chains could not be created either).
            if (outError.isNotEmpty()) outError += "\n";
            outError += "Too many chains in the saved state (maximum "
                      + juce::String (deps.maxChains) + "): the rest were skipped.";
            break;
        }

        if (! spec.hasState)
            continue;

        juce::String chainError;
        chain->setChainState (spec.state, chainError);
        if (chainError.isNotEmpty())
        {
            if (outError.isNotEmpty()) outError += "\n";
            outError += spec.errorLabel + " " + chainError;
        }
    }

    // The saved state may predate the id counter or contain duplicate
    // ids (hand-edited files); fix both so every chain has a stable,
    // unique id.
    if (obj->hasProperty ("nextChainId"))
        deps.nextChainId = juce::jmax (deps.nextChainId,
                                       static_cast<int> (obj->getProperty ("nextChainId")));
    ensureUniqueChainIds();
}

juce::var ChainStatePersistence::loadSavedState (juce::PropertiesFile& props)
{
    const auto newJson = props.getValue ("pluginChains");
    const auto oldJson = props.getValue ("pluginChain");

    auto parse = [](const juce::String& json) -> juce::var
    {
        return json.isNotEmpty() ? juce::JSON::parse (json) : juce::var();
    };
    const auto newVar = parse (newJson);
    const auto oldVar = parse (oldJson);

    // The current-format key is authoritative whenever it parses to a
    // valid bundle object at all: a pluginChains written by this build
    // (even one holding empty chains — the user removed the plugins)
    // always wins over the legacy pre-split key, which belongs to an
    // older save and can contain same-chain duplicates. The old key
    // only serves as a fallback for states that never saw the new
    // format.
    if (newVar.isObject())
        return newVar;
    return oldVar;
}

PluginChain* ChainStatePersistence::createChain (const juce::String& name,
                                                 int inputMask,
                                                 bool wantsMidi,
                                                 bool recordOnCapture)
{
    // Refuse past the owner's maximum: the processor's audio-thread snapshot
    // is reserved to that size, so extra chains would be silently truncated
    // (and never processed). Only the message thread mutates the list, so the
    // check-then-push below cannot race.
    if (deps.maxChains > 0)
    {
        const juce::ScopedLock sl (deps.chainLock);
        if (static_cast<int> (deps.chains.size()) >= deps.maxChains)
            return nullptr;
    }

    auto chain = std::make_shared<PluginChain> (deps.vst3);
    chain->setChainId (juce::String ("chain") + juce::String (deps.nextChainId++));
    chain->setName (name.isNotEmpty() ? name : juce::String ("Chain ") + juce::String (deps.nextChainId));
    chain->setInputMask (inputMask);
    chain->setWantsMidi (wantsMidi);
    chain->setRecordOnCapture (recordOnCapture);

    // Every chain (default, UI-created, or restore-created) must run
    // the persistence callback on change. Wiring it here — rather than
    // only on the constructor's default chains — is what makes slot
    // mutations (add/remove/bypass) on restored chains persist. Wired
    // before the prepare hook, which can already make a plugin notify.
    chain->onChanged = deps.onChainChanged;

    // Prepare with the live device config before publishing, so a chain
    // created after the device opened (addChain) hands its plugins the real
    // sample rate / block size instead of the chain's 44100/512 defaults.
    // The processor's hook skips this until the host has prepared the device
    // (restore-time chains are covered by the host's prepareToPlay), which
    // keeps the Neural DSP "no fake-then-real double prepare" rule intact.
    if (deps.prepareChain)
        deps.prepareChain (*chain);

    PluginChain* raw = chain.get();
    {
        const juce::ScopedLock sl (deps.chainLock);
        deps.chains.push_back (std::move (chain));
    }

    return raw;
}

void ChainStatePersistence::clearChains()
{
    std::vector<std::shared_ptr<PluginChain>> removed;
    {
        const juce::ScopedLock sl (deps.chainLock);
        // Copy then clear, so the vector keeps the capacity the audio thread
        // relies on (its snapshot reserve is sized to the same maximum).
        removed = deps.chains;
        deps.chains.clear();
    }

    for (auto& chain : removed)
    {
        if (chain == nullptr)
            continue;

        // Hand it to the owner for deferred destruction; the owner's
        // retirement closes the chain's plugin windows (exactly once — do not
        // do it here too) and keeps the chain alive until the audio thread
        // can no longer reference it. Without a hook (tests only) the chain
        // simply drops here.
        if (deps.retireChain)
            deps.retireChain (chain);
    }
}

void ChainStatePersistence::ensureUniqueChainIds()
{
    const juce::ScopedLock sl (deps.chainLock);

    std::vector<juce::String> rawIds;
    rawIds.reserve (deps.chains.size());
    for (auto& chain : deps.chains)
        rawIds.push_back (chain->getChainId());

    const auto fixedIds = ChainStateMigration::dedupeIds (rawIds, deps.nextChainId);
    for (size_t i = 0; i < deps.chains.size(); ++i)
        if (fixedIds[i] != rawIds[i])
            deps.chains[i]->setChainId (fixedIds[i]);
}
