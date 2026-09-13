#include "TestRunner.h"
#include "RestoreSelfHeal.h"

namespace
{
juce::File makeTempDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("bp_selfheal_" + juce::Uuid().toString());
    dir.createDirectory();
    return dir;
}

juce::PropertiesFile::Options testOptions()
{
    juce::PropertiesFile::Options opts;
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    opts.millisecondsBeforeSaving = 0;
    return opts;
}

void writeFile (const juce::File& file, const juce::String& text)
{
    file.getParentDirectory().createDirectory();
    file.replaceWithText (text);
}
}

BP_TEST (RestoreSelfHeal_parsesTheCrashOpDetail)
{
    const juce::String text =
        "BluePrinter crash diagnostics\n"
        "Operation: preparing plugin (finalizeAsyncLoad) Archetype Tim Henson X.vst3\n"
        "Faulting module: C:\\...\\X.vst3\n";
    BP_CHECK_EQ (RestoreSelfHeal::parseCrashOpDetail (text), juce::String ("Archetype Tim Henson X.vst3"));

    BP_CHECK_EQ (RestoreSelfHeal::parseCrashOpDetail ("Operation: no api here"), juce::String());
    BP_CHECK_EQ (RestoreSelfHeal::parseCrashOpDetail ("no op line"), juce::String());
}

BP_TEST (RestoreSelfHeal_parsesTheFreshLoadOpOnly)
{
    const auto now = juce::Time::currentTimeMillis();
    const auto raw = juce::String (now) + ":Bad Plugin.vst3";

    BP_CHECK_EQ (RestoreSelfHeal::parseLoadOpDetail (raw, juce::Time (now - 10000)),
                 juce::String ("Bad Plugin.vst3"));

    // Stale: the breadcrumb predates the anchor.
    BP_CHECK_EQ (RestoreSelfHeal::parseLoadOpDetail (raw, juce::Time (now + 10000)), juce::String());

    // Cleared breadcrumb (no name) and malformed values.
    BP_CHECK_EQ (RestoreSelfHeal::parseLoadOpDetail (juce::String (now) + ":", juce::Time (0)), juce::String());
    BP_CHECK_EQ (RestoreSelfHeal::parseLoadOpDetail ("notimestamp", juce::Time (0)), juce::String());
    BP_CHECK_EQ (RestoreSelfHeal::parseLoadOpDetail ({}, juce::Time (0)), juce::String());
}

BP_TEST (RestoreSelfHeal_blobsAllowedDependsOnACleanExit)
{
    const auto dir = makeTempDir();
    const auto settings = dir.getChildFile ("BluePrinter.settings");
    const auto markerAt = juce::Time::getCurrentTime();

    // No marker -> trusted.
    BP_CHECK (RestoreSelfHeal::blobsAllowed (false, {}, settings));

    // Marker set, but a clean exit (settings written) after it -> trusted.
    writeFile (settings, "clean");
    settings.setLastModificationTime (markerAt + juce::RelativeTime::seconds (5));
    BP_CHECK (RestoreSelfHeal::blobsAllowed (true, markerAt, settings));

    // Marker newer than the last clean exit -> self-heal.
    settings.setLastModificationTime (markerAt - juce::RelativeTime::seconds (5));
    BP_CHECK (! RestoreSelfHeal::blobsAllowed (true, markerAt, settings));

    // No settings file (DAW host) and marker at 0 -> self-heal.
    settings.deleteFile();
    BP_CHECK (! RestoreSelfHeal::blobsAllowed (true, markerAt, settings));

    dir.deleteRecursively();
}

BP_TEST (RestoreSelfHeal_quarantineIsCaseInsensitiveAndPersisted)
{
    const auto dir = makeTempDir();
    const auto propsFile = dir.getChildFile ("BluePrinter.properties");

    {
        juce::PropertiesFile props (propsFile, testOptions());
        RestoreSelfHeal heal;

        heal.addQuarantined (props, "Archetype Cory Wong X.vst3");
        heal.addQuarantined (props, "archetype cory wong x.VST3"); // duplicate

        BP_CHECK (heal.isQuarantined (props, "ARCHETYPE CORY WONG X.vst3"));
        BP_CHECK_EQ (heal.quarantineSnapshot (props).getArray()->size(), 1);
    }

    // Re-read from disk with a fresh object.
    {
        juce::PropertiesFile props (propsFile, testOptions());
        RestoreSelfHeal heal;
        BP_CHECK (heal.isQuarantined (props, "Archetype Cory Wong X.vst3"));

        heal.clearQuarantineForFile (props, "ARCHETYPE CORY WONG X.VST3");
        BP_CHECK (! heal.isQuarantined (props, "Archetype Cory Wong X.vst3"));
    }

    dir.deleteRecursively();
}

BP_TEST (RestoreSelfHeal_planSelfHealsWhenTheMarkerIsHot)
{
    const auto dir = makeTempDir();
    const auto diagnostics = dir.getChildFile ("diag");
    const auto settings = dir.getChildFile ("settings").getChildFile ("BluePrinter.settings");
    const auto crashInfo = diagnostics.getChildFile ("crash-info.txt");
    const auto propsFile = dir.getChildFile ("BluePrinter.properties");

    const auto now = juce::Time::getCurrentTime();
    writeFile (settings, "clean exit");
    settings.setLastModificationTime (now - juce::RelativeTime::seconds (500)); // old clean exit
    writeFile (crashInfo, "Operation: preparing plugin (finalizeAsyncLoad) Bad.vst3\n");
    crashInfo.setLastModificationTime (now); // fresh crash

    juce::PropertiesFile props (propsFile, testOptions());
    props.setValue ("chainRestoreCrashed", true);
    props.setValue ("chainRestoreMarkerTime",
                    juce::String ((now - juce::RelativeTime::seconds (100)).toMilliseconds()));
    props.setValue ("lastPluginLoadOp",
                    juce::String (now.toMilliseconds()) + ":Other.vst3");
    props.saveIfNeeded();

    RestoreSelfHeal heal ({ diagnostics, settings });
    const auto plan = heal.plan (props, now - juce::RelativeTime::seconds (600));

    BP_CHECK (! plan.blobsAllowed);                 // marker hot, no clean exit since
    BP_CHECK (plan.selfHeal);
    BP_CHECK_EQ (plan.crashDetail, juce::String ("Bad.vst3"));
    BP_CHECK_EQ (plan.loadOpDetail, juce::String ("Other.vst3"));

    // The marker is re-armed for the next launch.
    BP_CHECK (props.getBoolValue ("chainRestoreCrashed", false));

    dir.deleteRecursively();
}

BP_TEST (RestoreSelfHeal_skipsAccumulateUntilReset)
{
    RestoreSelfHeal heal;

    const auto first = heal.recordSkipped ("A.vst3", "Amp A");
    BP_CHECK (first.contains ("Amp A"));

    const auto second = heal.recordSkipped ("B.vst3", "Amp B");
    BP_CHECK (second.contains ("Amp A"));
    BP_CHECK (second.contains ("Amp B"));

    heal.resetSkipped();

    const auto third = heal.recordSkipped ("C.vst3", {});
    BP_CHECK (third.contains ("C.vst3"));
    BP_CHECK (! third.contains ("Amp A"));
}
