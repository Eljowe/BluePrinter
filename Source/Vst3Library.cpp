#include "Vst3Library.h"

#include <future>
#include <thread>

Vst3Library::Vst3Library() = default;

juce::var Vst3Library::scanFolder (const juce::File& folder, juce::String& outError)
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
        if (isBlocked (file))
            continue;

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

juce::var Vst3Library::describeVst3File (const juce::File& file, juce::String& outError)
{
    if (isBlocked (file))
    {
        outError = "Plugin is blocked: " + file.getFileName();
        return {};
    }

    // Third-party VST3 DLLs are arbitrary native code. findAllTypesForFile
    // loads the .vst3 and walks its module entry; a misbehaving plugin can
    // raise a C++ exception out of the factory call (some plugins do this
    // on expired licenses, missing redistributables, or a corrupted PE).
    // The describe path used to be called directly on the message thread
    // from the folder scanner, so a throw here would tear the host down.
    // Catch defensively and report; the async wrapper in
    // describeVst3FileAsync turns a hang/dialog into a clean timeout.
    try
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
    catch (const std::exception& e)
    {
        outError = juce::String ("Exception scanning ") + file.getFileName() + ": " + e.what();
    }
    catch (...)
    {
        outError = "Unknown exception scanning " + file.getFileName();
    }
    return {};
}

void Vst3Library::describeVst3FileAsync (const juce::File& file,
                                          int timeoutMs,
                                          DescribeCallback callback)
{
    // The worker thread runs the same describeVst3File used by
    // setChainState, so the result of "describe" is consistent across
    // the app: blocklist filter + VST3 factory call + per-plugin
    // exception handling. The try/catch in describeVst3File already
    // covers C++ exceptions thrown out of the factory; the worker
    // lambda catches anything that escapes that as a belt-and-braces.
    struct PendingDescribe
    {
        juce::File file;
        std::future<DescribeResult> future;
    };

    auto pending = std::make_shared<PendingDescribe>();
    pending->file = file;

    auto promise = std::make_shared<std::promise<DescribeResult>>();
    pending->future = promise->get_future();

    std::thread ([this, file, promise]() mutable
    {
        DescribeResult r;
        try
        {
            r.descriptions = describeVst3File (file, r.error);
        }
        catch (...)
        {
            // r.descriptions is already default-constructed empty.
            r.error = "Exception escaping describeVst3File for " + file.getFileName();
        }
        promise->set_value (std::move (r));
    }).detach();

    // Polling timer on the message thread. Fires every 50 ms to check
    // whether the worker finished; bails out with timedOut=true if the
    // deadline is reached first. The worker keeps running in the
    // background and its result is discarded when the future is dropped.
    struct DescribeWaiter : private juce::Timer
    {
        std::shared_ptr<PendingDescribe> pending;
        DescribeCallback callback;
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

                DescribeResult result;
                try
                {
                    result = pending->future.get();
                }
                catch (...)
                {
                    result = {};
                    result.error = "Exception reading scan result for " + pending->file.getFileName();
                }

                // Move the callback out and self-delete BEFORE invoking
                // it. The callback may free the last reference to the
                // editor (e.g. by resetting activeScan), and we don't
                // want to be running our own code after that.
                DescribeCallback cb = std::move (callback);
                delete this;

                if (cb)
                    cb (result);
                return;
            }

            if (juce::Time::currentTimeMillis() >= deadlineMs)
            {
                stopTimer();

                DescribeResult result;
                result.timedOut = true;
                result.error = "Timed out scanning: " + pending->file.getFileName();

                DescribeCallback cb = std::move (callback);
                delete this;

                if (cb)
                    cb (result);
            }
        }
    };

    auto* waiter = new DescribeWaiter();
    waiter->pending = pending;
    waiter->callback = std::move (callback);
    waiter->startWaiting (timeoutMs);
}

juce::File Vst3Library::getDefaultVst3Folder()
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

bool Vst3Library::isBlocked (const juce::File& file) const
{
    const juce::ScopedLock sl (blocklistLock);
    return blocklist.count (file.getFullPathName()) > 0;
}

void Vst3Library::addToBlocklist (const juce::File& file)
{
    bool changed = false;
    {
        const juce::ScopedLock sl (blocklistLock);
        changed = blocklist.insert (file.getFullPathName()).second;
    }
}

bool Vst3Library::removeFromBlocklist (const juce::File& file)
{
    bool changed = false;
    {
        const juce::ScopedLock sl (blocklistLock);
        changed = blocklist.erase (file.getFullPathName()) > 0;
    }
    return changed;
}

juce::StringArray Vst3Library::getBlocklist() const
{
    const juce::ScopedLock sl (blocklistLock);
    juce::StringArray result;
    for (const auto& path : blocklist)
        result.add (path);
    return result;
}

void Vst3Library::setBlocklist (const juce::StringArray& paths)
{
    {
        const juce::ScopedLock sl (blocklistLock);
        const size_t newSize = static_cast<size_t> (paths.size());
        blocklist.clear();
        for (const auto& path : paths)
            blocklist.insert (path);
    }
}

void Vst3Library::clearBlocklist()
{
    {
        const juce::ScopedLock sl (blocklistLock);
        blocklist.clear();
    }
}

void Vst3Library::setAvailablePlugins (const juce::var& plugins)
{
    {
        const juce::ScopedLock sl (availableLock);
        availablePlugins = plugins;
    }
}

juce::var Vst3Library::getAvailablePlugins() const
{
    const juce::ScopedLock sl (availableLock);
    return availablePlugins;
}
