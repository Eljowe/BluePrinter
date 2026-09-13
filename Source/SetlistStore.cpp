#include "SetlistStore.h"

#include <algorithm>

namespace
{
bool containsId (const std::vector<int>& ids, int id)
{
    return std::find (ids.begin(), ids.end(), id) != ids.end();
}
}

void SetlistStore::clear()
{
    setlists.clear();
    nextId = 1;
}

const SetlistEntry* SetlistStore::find (const juce::String& id) const
{
    for (const auto& s : setlists)
        if (s.id == id)
            return &s;
    return nullptr;
}

juce::String SetlistStore::create (const juce::String& name)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return {};

    SetlistEntry entry;
    entry.id = juce::String (nextId++);
    entry.name = trimmed;
    setlists.push_back (std::move (entry));
    return setlists.back().id;
}

bool SetlistStore::rename (const juce::String& id, const juce::String& name)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return false;

    for (auto& s : setlists)
    {
        if (s.id == id)
        {
            s.name = trimmed;
            return true;
        }
    }
    return false;
}

bool SetlistStore::remove (const juce::String& id)
{
    for (auto it = setlists.begin(); it != setlists.end(); ++it)
    {
        if (it->id == id)
        {
            setlists.erase (it);
            return true;
        }
    }
    return false;
}

bool SetlistStore::addSnippet (const juce::String& id, int snippetId)
{
    for (auto& s : setlists)
    {
        if (s.id == id)
        {
            if (! containsId (s.snippetIds, snippetId))
                s.snippetIds.push_back (snippetId);
            return true;
        }
    }
    return false;
}

bool SetlistStore::removeSnippet (const juce::String& id, int snippetId)
{
    for (auto& s : setlists)
    {
        if (s.id == id)
        {
            s.snippetIds.erase (std::remove (s.snippetIds.begin(), s.snippetIds.end(), snippetId),
                                s.snippetIds.end());
            return true;
        }
    }
    return false;
}

bool SetlistStore::setOrder (const juce::String& id, const std::vector<int>& ids)
{
    for (auto& s : setlists)
    {
        if (s.id != id)
            continue;

        std::vector<int> reordered;
        reordered.reserve (s.snippetIds.size());
        // Keep only ids that are already members, de-duplicated, so a stale
        // UI order can never inject an unknown snippet.
        for (int candidate : ids)
            if (containsId (s.snippetIds, candidate) && ! containsId (reordered, candidate))
                reordered.push_back (candidate);
        // Preserve any members the new order omitted, at the end.
        for (int existing : s.snippetIds)
            if (! containsId (reordered, existing))
                reordered.push_back (existing);

        s.snippetIds = std::move (reordered);
        return true;
    }
    return false;
}

bool SetlistStore::prune (const std::vector<int>& validIds)
{
    bool changed = false;
    for (auto& s : setlists)
    {
        const auto before = s.snippetIds.size();
        s.snippetIds.erase (
            std::remove_if (s.snippetIds.begin(), s.snippetIds.end(),
                            [&validIds](int id) { return ! containsId (validIds, id); }),
            s.snippetIds.end());
        if (s.snippetIds.size() != before)
            changed = true;
    }
    return changed;
}

juce::String SetlistStore::toJson() const
{
    juce::Array<juce::var> arr;
    for (const auto& s : setlists)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("id", s.id);
        obj->setProperty ("name", s.name);

        juce::Array<juce::var> ids;
        for (int snippetId : s.snippetIds)
            ids.add (snippetId);
        obj->setProperty ("ids", juce::var (ids));

        arr.add (juce::var (obj));
    }
    return juce::JSON::toString (juce::var (arr));
}

void SetlistStore::fromJson (const juce::String& json)
{
    clear();

    const auto parsed = juce::JSON::parse (json);
    const auto* arr = parsed.getArray();
    if (arr == nullptr)
        return;

    int maxId = 0;
    for (const auto& v : *arr)
    {
        const auto* obj = v.getDynamicObject();
        if (obj == nullptr)
            continue;

        SetlistEntry entry;
        entry.id = obj->getProperty ("id").toString().trim();
        entry.name = obj->getProperty ("name").toString().trim();
        if (entry.name.isEmpty())
            continue;

        if (auto* ids = obj->getProperty ("ids").getArray())
        {
            for (const auto& idVar : *ids)
            {
                const int snippetId = static_cast<int> (idVar);
                if (snippetId > 0 && ! containsId (entry.snippetIds, snippetId))
                    entry.snippetIds.push_back (snippetId);
            }
        }

        if (entry.id.isEmpty())
            entry.id = juce::String (maxId + 1);

        maxId = juce::jmax (maxId, entry.id.getIntValue());
        setlists.push_back (std::move (entry));
    }

    nextId = maxId + 1;
}
