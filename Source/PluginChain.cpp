#include "PluginChain.h"

#include <future>
#include <thread>
#include "PluginProcessor.h"

#include <future>
#include <thread>

PluginChain::PluginChain (Vst3Library& lib)
    : library (lib)
{
    // Register the VST3 format. addFormat takes ownership and deletes it
    // in the format manager's destructor.
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
}

PluginChain::~PluginChain()
{
    clear();
}

void PluginChain::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;

    const juce::ScopedLock sl (lock);
    for (auto& slot : slots)
    {
        if (slot->plugin != nullptr)
            slot->plugin->prepareToPlay (sampleRate, samplesPerBlock);
    }
}

void PluginChain::releaseResources()
{
    const juce::ScopedLock sl (lock);
    for (auto& slot : slots)
    {
        if (slot->plugin != nullptr)
            slot->plugin->releaseResources();
    }
}

void PluginChain::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // The chain runs plugins in order, in place. We use a snapshot of the
    // slot list so adding/removing plugins from the message thread mid-
    // block can't invalidate our iteration.
    std::vector<ChainSlot*> snapshot;
    {
        const juce::ScopedLock sl (lock);
        snapshot.reserve (slots.size());
        for (auto& slot : slots)
            if (slot->plugin != nullptr && ! slot->bypassed)
                snapshot.push_back (slot.get());
    }

    if (! wantsMidi.load (std::memory_order_acquire))
    {
        // MIDI pass-through switched off for this chain: hand the
        // plugins an empty buffer so note-aware FX can't see the
        // keyboard input.
        juce::MidiBuffer emptyMidi;
        for (auto* slot : snapshot)
            slot->plugin->processBlock (buffer, emptyMidi);
        return;
    }

    for (auto* slot : snapshot)
        slot->plugin->processBlock (buffer, midi);
}

juce::AudioPluginInstance* PluginChain::createInstance (const juce::File& file,
                                                        juce::String& outName,
                                                        juce::String& outError)
{
    juce::OwnedArray<juce::PluginDescription> types;
    BluePrinterAudioProcessor::setCrashOp ("scanning VST3 descriptions (findAllTypesForFile)", file.getFileName().toRawUTF8());
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        if (auto* format = formatManager.getFormat (i))
            format->findAllTypesForFile (types, file.getFullPathName());
    }

    if (types.isEmpty())
    {
        outError = "No VST3 plugin found in file: " + file.getFullPathName();
        return nullptr;
    }

    auto* desc = types.getFirst();
    BluePrinterAudioProcessor::setCrashOp ("instantiating plugin (createPluginInstance)", file.getFileName().toRawUTF8());
    auto instance = formatManager.createPluginInstance (*desc,
                                                        currentSampleRate,
                                                        currentBlockSize,
                                                        outError);
    if (instance != nullptr)
        outName = instance->getName();
    return instance.release();
}

int PluginChain::addPlugin (const juce::File& vst3File, juce::String& outError)
{
    // exists() (not existsAsFile()): the path may be a .vst3 bundle
    // directory rather than a loose binary file.
    if (! vst3File.exists())
    {
        outError = "File not found: " + vst3File.getFullPathName();
        return -1;
    }

    if (library.isBlocked (vst3File))
    {
        outError = "Plugin is blocked: " + vst3File.getFileName();
        return -1;
    }

    if (hasPluginFile (vst3File))
    {
        outError = "Plugin is already in this chain: " + vst3File.getFileName();
        return -1;
    }

    juce::String pluginName;
    auto* instance = createInstance (vst3File, pluginName, outError);
    if (instance == nullptr)
        return -1;

    // Listen for parameter/structure changes inside the plugin so the
    // owner can persist tweaks (onChanged). Attached here on the
    // message thread; removed before the plugin is destroyed.
    instance->addListener (this);

    BluePrinterAudioProcessor::setCrashOp ("preparing plugin (prepareToPlay)", vst3File.getFileName().toRawUTF8());
    instance->prepareToPlay (currentSampleRate, currentBlockSize);

    auto slot = std::make_unique<ChainSlot>();
    slot->plugin.reset (instance);
    slot->name = pluginName.isNotEmpty() ? pluginName : vst3File.getFileNameWithoutExtension();
    slot->path = vst3File.getFullPathName();

    {
        const juce::ScopedLock sl (lock);
        slots.push_back (std::move (slot));
    }

    if (onChanged)
        onChanged();
    return getNumPlugins() - 1;
}

bool PluginChain::hasPluginFile (const juce::File& file) const
{
    const juce::ScopedLock sl (lock);
    const auto canonical = file.getFullPathName();
    for (const auto& slot : slots)
        if (slot->path == canonical)
            return true;
    return false;
}

int PluginChain::finalizeAsyncLoad (std::unique_ptr<juce::AudioPluginInstance> instance,
                                    const juce::String& name,
                                    const juce::File& file)
{
    // Runs on the message thread (timer callback). prepareToPlay is
    // safe to call here; the chain's prepareToPlay will be called by
    // the host later with the actual sample rate, but we want the
    // plugin ready in case the host queries state before then.
    if (hasPluginFile (file))
        return -1;

    // Listen for parameter/structure changes inside the plugin so the
    // owner can persist tweaks (onChanged). Attached here on the
    // message thread; removed before the plugin is destroyed.
    instance->addListener (this);

    BluePrinterAudioProcessor::setCrashOp ("preparing plugin (finalizeAsyncLoad)", file.getFileName().toRawUTF8());
    instance->prepareToPlay (currentSampleRate, currentBlockSize);

    auto slot = std::make_unique<ChainSlot>();
    slot->plugin = std::move (instance);
    slot->name = name.isNotEmpty() ? name : file.getFileNameWithoutExtension();
    slot->path = file.getFullPathName();

    int newIndex = 0;
    {
        const juce::ScopedLock sl (lock);
        slots.push_back (std::move (slot));
        newIndex = static_cast<int> (slots.size()) - 1;
    }

    if (onChanged)
        onChanged();
    return newIndex;
}

bool PluginChain::addPluginAsync (const juce::File& vst3File,
                                  int timeoutMs,
                                  LoadCallback callback)
{
    // exists() (not existsAsFile()): the path may be a .vst3 bundle
    // directory rather than a loose binary file.
    if (! vst3File.exists())
    {
        if (callback) callback (-1, {}, "File not found: " + vst3File.getFullPathName(), false);
        return false;
    }

    if (library.isBlocked (vst3File))
    {
        if (callback) callback (-1, {}, "Plugin is blocked: " + vst3File.getFileName(), false);
        return false;
    }

    if (hasPluginFile (vst3File))
    {
        if (callback) callback (-1, {}, "Plugin is already in this chain: " + vst3File.getFileName(), false);
        return false;
    }

    // Result struct returned by the worker via the future. Uses
    // std::string for the metadata so the struct is properly movable
    // (juce::String has no move assignment). The waiter converts to
    // juce::String on the message thread. The future is std::future
    // (not shared_future) so get() returns by value rather than by
    // const reference, which is required to move the unique_ptr out.
    struct LoadResult
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        std::string name;
        std::string error;
    };

    // Shared state between the worker thread and the polling timer.
    // The promise is captured by the worker; the future is held by
    // the waiter via the `pending` shared_ptr.
    struct PendingLoad
    {
        juce::File file;
        std::future<LoadResult> future;
    };

    auto pending = std::make_shared<PendingLoad>();
    pending->file = vst3File;

    auto promise = std::make_shared<std::promise<LoadResult>>();
    pending->future = promise->get_future();

    // Run the synchronous createInstance on a worker thread. The
    // createInstance helper writes into juce::String references; we
    // adapt by writing into a juce::String and copying the contents
    // into std::string before setting the promise value. The
    // std::string crosses the thread boundary safely via the promise.
    std::thread ([this, vst3File, promise]() mutable
    {
        LoadResult r;
        juce::String name, error;
        r.instance.reset (createInstance (vst3File, name, error));
        r.name  = name.toStdString();
        r.error = error.toStdString();
        promise->set_value (std::move (r));
    }).detach();

    // Polling timer on the message thread. Fires every 50 ms to check
    // whether the worker finished, and bails out with timedOut=true if
    // the deadline is reached first.
    struct LoadWaiter : private juce::Timer
    {
        PluginChain* chain = nullptr;
        std::shared_ptr<PendingLoad> pending;
        LoadCallback callback;
        int64_t deadlineMs = 0;

        void startWaiting (int timeoutMs)
        {
            deadlineMs = juce::Time::currentTimeMillis() + timeoutMs;
            startTimer (50);
        }

        void timerCallback() override
        {
            if (pending->future.wait_for (std::chrono::milliseconds (0)) == std::future_status::ready)
            {
                stopTimer();

                // Pull the result out of the future on the message
                // thread. std::future::get() returns by value, so
                // move semantics work for the unique_ptr member.
                LoadResult result;
                try
                {
                    result = pending->future.get();
                }
                catch (...)
                {
                    result = {};
                }

                const juce::String name  = juce::String (std::move (result.name));
                const juce::String error = juce::String (std::move (result.error));

                int slotIndex = -1;
                if (result.instance != nullptr)
                    slotIndex = chain->finalizeAsyncLoad (std::move (result.instance),
                                                          name,
                                                          pending->file);

                if (callback)
                    callback (slotIndex, name,
                              slotIndex < 0 ? error : juce::String(),
                              false);
                delete this;
                return;
            }

            if (juce::Time::currentTimeMillis() >= deadlineMs)
            {
                stopTimer();
                // Worker still running. We abandon it: the future will
                // eventually resolve and the resulting instance will be
                // destroyed when the future is destroyed. The worker
                // thread itself is not killed (unsafe in C++).
                if (callback)
                    callback (-1, {},
                              "Timed out loading plugin: " + pending->file.getFullPathName(),
                              true);
                delete this;
            }
        }
    };

    auto* waiter = new LoadWaiter();
    waiter->chain = this;
    waiter->pending = pending;
    waiter->callback = std::move (callback);
    waiter->startWaiting (timeoutMs);
    return true;
}

bool PluginChain::removePlugin (int index)
{
    const int removedIndex = index;
    {
        const juce::ScopedLock sl (lock);
        if (index < 0 || index >= static_cast<int> (slots.size()))
            return false;
        if (slots[index]->plugin != nullptr)
        {
            slots[index]->plugin->removeListener (this);
            slots[index]->plugin->releaseResources();
        }
        slots.erase (slots.begin() + index);
    }
    if (onSlotRemoved)
        onSlotRemoved (removedIndex);
    if (onChanged)
        onChanged();
    return true;
}

bool PluginChain::setBypass (int index, bool shouldBypass)
{
    {
        const juce::ScopedLock sl (lock);
        if (index < 0 || index >= static_cast<int> (slots.size()))
            return false;
        slots[index]->bypassed = shouldBypass;
    }
    if (onChanged)
        onChanged();
    return true;
}

bool PluginChain::movePlugin (int fromIndex, int toIndex)
{
    {
        const juce::ScopedLock sl (lock);
        const int n = static_cast<int> (slots.size());
        if (fromIndex < 0 || fromIndex >= n) return false;
        if (toIndex   < 0 || toIndex   >= n) return false;
        if (fromIndex == toIndex) return true;

        auto slot = std::move (slots[static_cast<size_t> (fromIndex)]);
        slots.erase (slots.begin() + fromIndex);
        slots.insert (slots.begin() + toIndex, std::move (slot));
    }
    if (onChanged)
        onChanged();
    return true;
}

void PluginChain::clear()
{
    std::vector<int> removedIndices;
    {
        const juce::ScopedLock sl (lock);
        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (slots[i]->plugin != nullptr)
            {
                slots[i]->plugin->removeListener (this);
                slots[i]->plugin->releaseResources();
            }
            removedIndices.push_back (static_cast<int> (i));
        }
        slots.clear();
        pendingSlots.clear();
    }
    if (onSlotRemoved)
    {
        for (int idx : removedIndices)
            onSlotRemoved (idx);
    }
    if (onChanged)
        onChanged();
}

int PluginChain::getNumPlugins() const
{
    const juce::ScopedLock sl (lock);
    return static_cast<int> (slots.size());
}

juce::AudioPluginInstance* PluginChain::getPlugin (int index) const
{
    const juce::ScopedLock sl (lock);
    if (index < 0 || index >= static_cast<int> (slots.size()))
        return nullptr;
    return slots[static_cast<size_t> (index)]->plugin.get();
}

juce::String PluginChain::getSlotName (int index) const
{
    const juce::ScopedLock sl (lock);
    if (index < 0 || index >= static_cast<int> (slots.size()))
        return {};
    return slots[static_cast<size_t> (index)]->name;
}

juce::String PluginChain::getSlotPath (int index) const
{
    const juce::ScopedLock sl (lock);
    if (index < 0 || index >= static_cast<int> (slots.size()))
        return {};
    return slots[static_cast<size_t> (index)]->path;
}

bool PluginChain::isSlotBypassed (int index) const
{
    const juce::ScopedLock sl (lock);
    if (index < 0 || index >= static_cast<int> (slots.size()))
        return false;
    return slots[static_cast<size_t> (index)]->bypassed;
}

juce::var PluginChain::getChainState() const
{
    auto* obj = new juce::DynamicObject();

    // Per-chain identity and routing config. Inputs are serialized as
    // an array of channel indices so a future multi-channel bus slots
    // in without a format break; MIDI channels are 1-based.
    obj->setProperty ("id", chainId);
    obj->setProperty ("name", name);
    juce::Array<juce::var> inputArray;
    const int mask = inputMask.load (std::memory_order_acquire);
    for (int ch = 0; ch < 8; ++ch)
        if ((mask & (1 << ch)) != 0)
            inputArray.add (ch);
    obj->setProperty ("inputs", inputArray);
    obj->setProperty ("recordOnCapture", recordOnCapture.load (std::memory_order_acquire));
    obj->setProperty ("volume", volumeDb.load (std::memory_order_acquire));
    obj->setProperty ("muted", muted.load (std::memory_order_acquire));

    juce::Array<juce::var> midiChannels;
    const uint16_t midiMask = midiChannelsMask.load (std::memory_order_acquire);
    for (int ch = 1; ch <= 16; ++ch)
        if ((midiMask & (1u << (ch - 1))) != 0)
            midiChannels.add (ch);
    obj->setProperty ("midiChannels", midiChannels);

    const juce::ScopedLock sl (lock);
    juce::Array<juce::var> slotArray;
    for (const auto& slot : slots)
    {
        auto* slotObj = new juce::DynamicObject();
        slotObj->setProperty ("path",     slot->path);
        slotObj->setProperty ("bypassed", slot->bypassed);
        slotObj->setProperty ("name",     slot->name);

        if (slot->plugin != nullptr)
        {
            juce::MemoryBlock stateData;
            slot->plugin->getStateInformation (stateData);
            slotObj->setProperty ("state", stateData.toBase64Encoding());
        }

        slotArray.add (juce::var (slotObj));
    }
    obj->setProperty ("slots", slotArray);
    obj->setProperty ("pending", static_cast<int> (pendingSlots.size()));
    obj->setProperty ("wantsMidi", wantsMidi.load (std::memory_order_acquire));
    return juce::var (obj);
}

void PluginChain::setChainState (const juce::var& state, juce::String& outError)
{
    clear();

    auto* obj = state.getDynamicObject();
    if (obj == nullptr)
        return;

    // Identity + routing config. Missing fields (pre-multi-chain
    // states) keep the defaults set by the processor.
    if (obj->hasProperty ("id"))
        chainId = obj->getProperty ("id").toString();
    if (obj->hasProperty ("name"))
        name = obj->getProperty ("name").toString();
    if (obj->hasProperty ("inputs"))
    {
        if (auto* inputArray = obj->getProperty ("inputs").getArray())
        {
            int mask = ChainInputNone;
            for (const auto& v : *inputArray)
            {
                const int ch = static_cast<int> (v);
                if (ch >= 0 && ch < 8)
                    mask |= (1 << ch);
            }
            inputMask.store (mask, std::memory_order_release);
        }
        else if (obj->getProperty ("inputs").isInt() || obj->getProperty ("inputs").isDouble())
        {
            // Old integer-mask shape, kept for robustness.
            inputMask.store (static_cast<int> (obj->getProperty ("inputs")),
                             std::memory_order_release);
        }
    }
    if (obj->hasProperty ("recordOnCapture"))
        recordOnCapture.store (static_cast<bool> (obj->getProperty ("recordOnCapture")),
                               std::memory_order_release);
    if (obj->hasProperty ("volume"))
        volumeDb.store (static_cast<float> (obj->getProperty ("volume")),
                        std::memory_order_release);
    if (obj->hasProperty ("muted"))
        muted.store (static_cast<bool> (obj->getProperty ("muted")),
                     std::memory_order_release);
    if (obj->hasProperty ("midiChannels"))
    {
        uint16_t midiMask = 0;
        if (auto* midiArray = obj->getProperty ("midiChannels").getArray())
        {
            for (const auto& v : *midiArray)
            {
                const int ch = static_cast<int> (v);
                if (ch >= 1 && ch <= 16)
                    midiMask = static_cast<uint16_t> (midiMask | (1u << (ch - 1)));
            }
        }
        midiChannelsMask.store (midiMask, std::memory_order_release);
    }

    // MIDI pass-through preference. Missing in states saved before the
    // toggle existed — leave the default (true) to preserve old behaviour.
    if (obj->hasProperty ("wantsMidi"))
        wantsMidi.store (static_cast<bool> (obj->getProperty ("wantsMidi")),
                         std::memory_order_release);

    auto slotArray = obj->getProperty ("slots");
    if (! slotArray.isArray())
        return;

    juce::StringArray failedPaths;
    juce::StringArray failedReasons;

    for (int i = 0; i < slotArray.size(); ++i)
    {
        auto* slotObj = slotArray[i].getDynamicObject();
        if (slotObj == nullptr)
            continue;

        const juce::String path = slotObj->getProperty ("path").toString();
        if (path.isEmpty())
            continue;

        const juce::File file (path);
        if (library.isBlocked (file))
        {
            // The blocklist already covers this; don't re-attempt to load.
            failedPaths.add (path);
            failedReasons.add ("blocked");
            continue;
        }

        // Same-chain duplicate (see hasPluginFile). Two instances of the
        // same .vst3 in one chain crash some plugins (Neural DSP "X"
        // amp sims), so the second one is skipped here too — restoring
        // an older saved state with a duplicate slot must never take the
        // whole app down at startup.
        if (hasPluginFile (file))
        {
            failedPaths.add (path);
            failedReasons.add ("duplicate of an earlier plugin in this chain");
            continue;
        }

        // DEFERRED LOAD. The slot is queued, not instantiated: the
        // processor's restore driver (timerCallback) loads pending
        // slots one per message-loop turn. Synchronously instantiating
        // several plugins in a row keeps the message thread inside
        // plugin code for hundreds of ms; a window message the plugins
        // queue during their own setup then gets dispatched reentrantly
        // and crashes some plugins (Neural DSP "X" amp sims — heap fault
        // in the first instance's window proc). One slot per loop turn
        // gives every plugin's pending messages an idle moment to fire.
        // The bypass flag and the saved state blob travel with the slot
        // and are applied by the driver after the instance is ready.
        PendingSlot pending;
        pending.file = file;
        pending.name = slotObj->getProperty ("name").toString();
        pending.bypassed = static_cast<bool> (slotObj->getProperty ("bypassed"));
        pending.stateBase64 = slotObj->getProperty ("state").toString();
        pendingSlots.push_back (std::move (pending));
    }

    if (! failedPaths.isEmpty())
    {
        // Surface a single-line summary via outError. Callers that want
        // per-plugin details can call Vst3Library::getBlocklist() and
        // ask the user whether to add the failures to it.
        outError = "Skipped " + juce::String (failedPaths.size())
                 + " plugin(s) (use the blocklist to skip them in future): "
                 + failedPaths.joinIntoString (", ");
    }

    // Stash for getLastRestoreError() so the UI can show it on open.
    {
        const juce::ScopedLock sl (restoreErrorLock);
        lastRestoreError = outError;
    }
}

bool PluginChain::hasPendingSlots() const
{
    const juce::ScopedLock sl (lock);
    return ! pendingSlots.empty();
}

PluginChain::PendingSlot PluginChain::popPendingSlot()
{
    const juce::ScopedLock sl (lock);
    PendingSlot slot;
    if (! pendingSlots.empty())
    {
        slot = std::move (pendingSlots.front());
        pendingSlots.erase (pendingSlots.begin());
    }
    return slot;
}

juce::String PluginChain::getLastRestoreError() const
{
    const juce::ScopedLock sl (restoreErrorLock);
    return lastRestoreError;
}

void PluginChain::clearLastRestoreError()
{
    const juce::ScopedLock sl (restoreErrorLock);
    lastRestoreError.clear();
}
