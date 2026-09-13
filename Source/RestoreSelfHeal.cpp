#include "RestoreSelfHeal.h"

RestoreSelfHeal::Paths RestoreSelfHeal::defaultPaths()
{
    const auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    Paths p;
    p.diagnosticsFolder = appData.getChildFile ("Retrokielto");
    p.settingsFile      = appData.getChildFile ("BluePrinter").getChildFile ("BluePrinter.settings");
    return p;
}

RestoreSelfHeal::RestoreSelfHeal()
    : RestoreSelfHeal (defaultPaths())
{
}

RestoreSelfHeal::RestoreSelfHeal (Paths pathsToUse)
    : paths (std::move (pathsToUse))
{
}

//==============================================================================
juce::String RestoreSelfHeal::parseCrashOpDetail (const juce::String& crashInfoText)
{
    for (const auto& line : juce::StringArray::fromLines (crashInfoText))
    {
        if (! line.trim().startsWith ("Operation: "))
            continue;

        const auto op = line.trim().substring (juce::String ("Operation: ").length()).trim();
        // The op reads "<human step> (<api>) <plugin detail>"; split at the
        // last ") " so plugin names containing parens survive.
        const int split = op.lastIndexOf (") ");
        return split >= 0 ? op.substring (split + 2).trim() : juce::String();
    }
    return {};
}

juce::String RestoreSelfHeal::parseLoadOpDetail (const juce::String& rawValue, const juce::Time& anchor)
{
    if (rawValue.isEmpty())
        return {};

    const int colon = rawValue.indexOfChar (':');
    if (colon <= 0 || colon == rawValue.length() - 1)
        return {};

    const auto name = rawValue.substring (colon + 1).trim();
    if (name.isEmpty())
        return {};

    const auto writtenAt = juce::Time (rawValue.substring (0, colon).getLargeIntValue());
    if (writtenAt.toMilliseconds() <= 0 || writtenAt <= anchor)
        return {};

    return name;
}

bool RestoreSelfHeal::blobsAllowed (bool markerWasSet, const juce::Time& markerSetAt,
                                    const juce::File& settingsFile)
{
    if (! markerWasSet)
        return true;

    // The settings file is only ever written by a clean exit, so a marker
    // older than its mtime is stale: the session that set it ended cleanly and
    // the on-disk blobs were never corrupted (persistence is suppressed for
    // the whole restore). DAW hosts have no settings file; the marker is then
    // honored.
    if (markerSetAt.toMilliseconds() > 0
        && settingsFile.existsAsFile()
        && settingsFile.getLastModificationTime() > markerSetAt)
        return true;

    return false;
}

juce::String RestoreSelfHeal::readFreshCrashOpDetail (const juce::Time& anchor) const
{
    const auto crashFile = paths.diagnosticsFolder.getChildFile ("crash-info.txt");
    if (! crashFile.existsAsFile())
        return {};
    if (crashFile.getLastModificationTime() <= anchor)
        return {};
    return parseCrashOpDetail (crashFile.loadFileAsString());
}

RestoreSelfHeal::Plan RestoreSelfHeal::plan (juce::PropertiesFile& props,
                                             const juce::Time& propsLaunchMtime)
{
    // Decide once per process whether the saved state blobs are trusted. The
    // marker is read BEFORE this call writes it (a second applyChainState in
    // the same launch reuses the first decision).
    if (! decisionMade)
    {
        decisionMade = true;
        const bool markerWasSet = props.getBoolValue ("chainRestoreCrashed", false);
        // Stored as a string: epoch millis exceed PropertiesFile's 32-bit
        // getIntValue.
        const auto markerSetAt = juce::Time (props.getValue ("chainRestoreMarkerTime").getLargeIntValue());
        cachedBlobsAllowed = blobsAllowed (markerWasSet, markerSetAt, paths.settingsFile);
    }

    // Fallback freshness anchor: the properties mtime right now, before this
    // session's marker write. (The processor captured `propsLaunchMtime`
    // before ANY write this session; on startup persistLibraryFolder has
    // usually already bumped the file by the time we get here.)
    const auto propsMtimeBefore = props.getFile().getLastModificationTime();
    setCrashMarker (props);

    // Crash diagnostics are read on EVERY launch (not just when the marker was
    // set): a crash outside a deferred restore — a manual plugin add, or a
    // plugin dying after the restore drained — leaves the marker clear, and
    // those crashes need the quarantine just as much. Trust anchor: the LAST
    // CLEAN EXIT (settings mtime), because a crashed session writes the marker
    // into the properties AFTER its crash diagnostics landed; the settings
    // file is only ever written by a clean exit, so any crash-info newer than
    // it belongs to the session that just died. DAW hosts have no settings
    // file; fall back to the launch-time properties mtime.
    const auto anchor = paths.settingsFile.existsAsFile()
                            ? paths.settingsFile.getLastModificationTime()
                            : (propsLaunchMtime != juce::Time() ? propsLaunchMtime : propsMtimeBefore);

    Plan result;
    result.blobsAllowed = cachedBlobsAllowed;
    result.crashDetail  = readFreshCrashOpDetail (anchor);
    // Fail-fast crashes (0xC0000409) never reach the exception filter, so
    // crash-info.txt can stay frozen at an older crash while a later,
    // unrecorded one is the real killer. The lastPluginLoadOp property closes
    // that gap.
    result.loadOpDetail = parseLoadOpDetail (props.getValue ("lastPluginLoadOp"), anchor);
    result.selfHeal = ! result.blobsAllowed
                   || result.crashDetail.isNotEmpty()
                   || result.loadOpDetail.isNotEmpty();

    lastCrashDetail  = result.crashDetail;
    lastLoadOpDetail = result.loadOpDetail;
    return result;
}

void RestoreSelfHeal::setCrashMarker (juce::PropertiesFile& props)
{
    props.setValue ("chainRestoreCrashed", true);
    props.setValue ("chainRestoreMarkerTime", juce::String (juce::Time::currentTimeMillis()));
    props.saveIfNeeded();
}

void RestoreSelfHeal::clearCrashMarker (juce::PropertiesFile& props)
{
    props.setValue ("chainRestoreCrashed", false);
    props.saveIfNeeded();
}

//==============================================================================
void RestoreSelfHeal::ensureQuarantineLoaded (juce::PropertiesFile& props)
{
    if (quarantineLoaded)
        return;
    quarantineLoaded = true;

    const auto arr = juce::JSON::parse (props.getValue ("pluginQuarantine"));
    if (const auto* a = arr.getArray())
        for (const auto& v : *a)
            if (v.toString().trim().isNotEmpty())
                quarantine.addIfNotAlreadyThere (v.toString().trim());
}

void RestoreSelfHeal::saveQuarantine (juce::PropertiesFile& props)
{
    juce::Array<juce::var> arr;
    for (const auto& n : quarantine)
        arr.add (juce::var (n));
    props.setValue ("pluginQuarantine", juce::JSON::toString (juce::var (arr)));
    props.saveIfNeeded();
}

bool RestoreSelfHeal::isQuarantined (juce::PropertiesFile& props, const juce::String& fileName)
{
    ensureQuarantineLoaded (props);
    return quarantine.indexOf (fileName, true) >= 0;
}

void RestoreSelfHeal::addQuarantined (juce::PropertiesFile& props, const juce::String& fileName)
{
    ensureQuarantineLoaded (props);
    if (quarantine.indexOf (fileName, true) >= 0)
        return;
    quarantine.add (fileName);
    saveQuarantine (props);
}

void RestoreSelfHeal::clearQuarantineForFile (juce::PropertiesFile& props, const juce::String& fileName)
{
    ensureQuarantineLoaded (props);
    if (quarantine.indexOf (fileName, true) < 0)
        return;

    // Case-insensitive (Windows file names): the UI matches case-insensitively
    // too, so a stored/scan case difference must not leave a stuck entry.
    bool changed = false;
    for (int i = quarantine.size(); --i >= 0;)
    {
        if (quarantine[i].equalsIgnoreCase (fileName))
        {
            quarantine.remove (i);
            changed = true;
        }
    }
    if (changed)
        saveQuarantine (props);
}

juce::var RestoreSelfHeal::quarantineSnapshot (juce::PropertiesFile& props)
{
    ensureQuarantineLoaded (props);

    juce::Array<juce::var> arr;
    for (const auto& n : quarantine)
        arr.add (juce::var (n));
    return juce::var (arr);
}

//==============================================================================
void RestoreSelfHeal::notifyLoadStarting (juce::PropertiesFile& props, const juce::String& fileName)
{
    props.setValue ("lastPluginLoadOp",
                    juce::String (juce::Time::currentTimeMillis()) + ":" + fileName);
    props.saveIfNeeded();
}

void RestoreSelfHeal::notifyLoadFinished (juce::PropertiesFile& props)
{
    // Empty detail (just the timestamp) marks "no load in flight".
    props.setValue ("lastPluginLoadOp",
                    juce::String (juce::Time::currentTimeMillis()) + ":");
    props.saveIfNeeded();
}

//==============================================================================
juce::String RestoreSelfHeal::recordSkipped (const juce::String& fileName,
                                             const juce::String& pluginName)
{
    const auto shown = pluginName.isNotEmpty() ? pluginName : fileName;
    if (! skippedThisRestore.contains (shown))
        skippedThisRestore.add (shown);

    return "Skipped "
        + skippedThisRestore.joinIntoString (", ")
        + " — it crashed BluePrinter on a previous launch. Re-add it from a chain's plugin list to try again.";
}
