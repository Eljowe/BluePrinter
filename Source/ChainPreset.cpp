#include "ChainPreset.h"

namespace ChainPreset
{
namespace
{
    // The rig fields a preset keeps from a chain state (and accepts back).
    const char* const rigKeys[] = { "slots", "volume", "muted", "wantsMidi", "midiChannels" };
}

juce::StringArray knownKeys()
{
    juce::StringArray keys { "version", "name" };
    for (auto* key : rigKeys)
        keys.add (key);
    return keys;
}

juce::var makeDocument (const juce::String& presetName, const juce::var& chainState)
{
    auto* doc = new juce::DynamicObject();
    doc->setProperty ("version", currentVersion);
    doc->setProperty ("name", presetName);

    if (auto* src = chainState.getDynamicObject())
        for (auto* key : rigKeys)
            if (src->hasProperty (key))
                doc->setProperty (key, src->getProperty (key));

    return juce::var (doc);
}

Validation validateDocument (const juce::var& document)
{
    Validation v;

    auto* obj = document.getDynamicObject();
    if (obj == nullptr)
    {
        v.error = "not a chain preset document";
        return v;
    }

    if (! obj->hasProperty ("version"))
    {
        v.error = "missing preset version";
        return v;
    }

    const int version = static_cast<int> (obj->getProperty ("version"));
    v.version = version;
    if (version != currentVersion)
    {
        v.error = "unsupported preset version " + juce::String (version)
                + " (expected " + juce::String (currentVersion) + ")";
        return v;
    }

    juce::StringArray unknown;
    const auto known = knownKeys();
    for (const auto& prop : obj->getProperties())
        if (! known.contains (prop.name.toString()))
            unknown.add (prop.name.toString());

    if (! unknown.isEmpty())
        v.warning = "ignored unknown preset field(s): " + unknown.joinIntoString (", ");

    v.ok = true;
    return v;
}

juce::var getPayload (const juce::var& document)
{
    auto* obj = document.getDynamicObject();
    if (obj == nullptr)
        return {};

    auto* payload = new juce::DynamicObject();
    for (auto* key : rigKeys)
        if (obj->hasProperty (key))
            payload->setProperty (key, obj->getProperty (key));

    return juce::var (payload);
}

juce::String getPresetName (const juce::var& document)
{
    if (auto* obj = document.getDynamicObject())
        return obj->getProperty ("name").toString();
    return {};
}

juce::String sanitisePresetFileName (const juce::String& presetName)
{
    juce::String out;
    for (auto c : presetName)
    {
        if (c < 32)
            continue;
        if (juce::String ("<>:\"/\\|?*").containsChar (c))
            continue;
        out += juce::String::charToString (c);
    }

    out = out.trim();
    while (out.endsWithChar ('.'))
        out = out.dropLastCharacters (1);

    if (out.isEmpty())
        out = "preset";

    // Windows reserved device names can't be file stems.
    static const char* reserved[] = { "CON", "PRN", "AUX", "NUL",
                                      "COM1", "COM2", "COM3", "COM4", "COM5",
                                      "COM6", "COM7", "COM8", "COM9",
                                      "LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
                                      "LPT6", "LPT7", "LPT8", "LPT9" };
    for (auto* r : reserved)
        if (out.equalsIgnoreCase (r))
        {
            out = "_" + out;
            break;
        }

    return out.substring (0, 120);
}
}
