#include "PluginChain.h"

PluginChain::PluginChain()
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

    for (auto* slot : snapshot)
        slot->plugin->processBlock (buffer, midi);
}

juce::AudioPluginInstance* PluginChain::createInstance (const juce::File& file,
                                                        juce::String& outName,
                                                        juce::String& outError)
{
    juce::OwnedArray<juce::PluginDescription> types;
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
    if (! vst3File.existsAsFile())
    {
        outError = "File not found: " + vst3File.getFullPathName();
        return -1;
    }

    juce::String pluginName;
    auto* instance = createInstance (vst3File, pluginName, outError);
    if (instance == nullptr)
        return -1;

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

bool PluginChain::removePlugin (int index)
{
    const int removedIndex = index;
    {
        const juce::ScopedLock sl (lock);
        if (index < 0 || index >= static_cast<int> (slots.size()))
            return false;
        if (slots[index]->plugin != nullptr)
            slots[index]->plugin->releaseResources();
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

void PluginChain::clear()
{
    std::vector<int> removedIndices;
    {
        const juce::ScopedLock sl (lock);
        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (slots[i]->plugin != nullptr)
                slots[i]->plugin->releaseResources();
            removedIndices.push_back (static_cast<int> (i));
        }
        slots.clear();
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
    return juce::var (obj);
}

void PluginChain::setChainState (const juce::var& state, juce::String& outError)
{
    clear();

    auto* obj = state.getDynamicObject();
    if (obj == nullptr)
        return;

    auto slotArray = obj->getProperty ("slots");
    if (! slotArray.isArray())
        return;

    for (int i = 0; i < slotArray.size(); ++i)
    {
        auto* slotObj = slotArray[i].getDynamicObject();
        if (slotObj == nullptr)
            continue;

        const juce::String path = slotObj->getProperty ("path").toString();
        if (path.isEmpty())
            continue;

        const int index = addPlugin (juce::File (path), outError);
        if (index < 0)
            continue;

        setBypass (index, static_cast<bool> (slotObj->getProperty ("bypassed")));

        const juce::String stateBase64 = slotObj->getProperty ("state").toString();
        if (stateBase64.isNotEmpty())
        {
            juce::MemoryBlock stateData;
            if (stateData.fromBase64Encoding (stateBase64))
            {
                const juce::ScopedLock sl (lock);
                if (index >= 0 && index < static_cast<int> (slots.size())
                    && slots[static_cast<size_t> (index)]->plugin != nullptr)
                {
                    slots[static_cast<size_t> (index)]->plugin->setStateInformation (
                        stateData.getData(), static_cast<int> (stateData.getSize()));
                }
            }
        }
    }
}

juce::var PluginChain::describeVst3File (const juce::File& file, juce::String& outError)
{
    juce::AudioPluginFormatManager scanner;
    scanner.addFormat (std::make_unique<juce::VST3PluginFormat>());

    juce::OwnedArray<juce::PluginDescription> types;
    for (int i = 0; i < scanner.getNumFormats(); ++i)
    {
        if (auto* format = scanner.getFormat (i))
            format->findAllTypesForFile (types, file.getFullPathName());
    }

    if (types.isEmpty())
        return {};

    juce::Array<juce::var> pluginArray;
    for (auto* desc : types)
    {
        auto* pluginObj = new juce::DynamicObject();
        pluginObj->setProperty ("name",         desc->name);
        pluginObj->setProperty ("path",         file.getFullPathName());
        pluginObj->setProperty ("manufacturer", desc->manufacturerName);
        pluginObj->setProperty ("category",     desc->category);
        pluginArray.add (juce::var (pluginObj));
    }
    return juce::var (pluginArray);
}

juce::var PluginChain::scanFolder (const juce::File& folder, juce::String& outError)
{
    auto* result = new juce::DynamicObject();
    result->setProperty ("folder", folder.getFullPathName());

    juce::Array<juce::var> pluginArray;

    if (! folder.isDirectory())
    {
        outError = "Folder not found: " + folder.getFullPathName();
        result->setProperty ("plugins", pluginArray);
        return juce::var (result);
    }

    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.vst3");

    for (const auto& file : files)
    {
        juce::String perFileError;
        if (auto* arr = describeVst3File (file, perFileError).getArray())
        {
            for (const auto& v : *arr)
                pluginArray.add (v);
        }
    }

    result->setProperty ("plugins", pluginArray);
    return juce::var (result);
}

juce::File PluginChain::getDefaultVst3Folder()
{
    juce::VST3PluginFormat format;
    auto paths = format.getDefaultLocationsToSearch();
    for (int i = 0; i < paths.getNumPaths(); ++i)
    {
        juce::File f (paths[i]);
        if (f.isDirectory())
            return f;
    }
    if (paths.getNumPaths() > 0)
        return juce::File (paths[0]);

    // Fallbacks per platform.
   #if JUCE_WINDOWS
    return juce::File ("C:\\Program Files\\Common Files\\VST3");
   #elif JUCE_MAC
    return juce::File ("/Library/Audio/Plug-Ins/VST3");
   #else
    return juce::File ("~/.vst3");
   #endif
}
