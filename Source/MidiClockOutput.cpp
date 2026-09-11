#include "MidiClockOutput.h"

juce::StringArray MidiClockOutput::getAvailableDeviceNames()
{
    juce::StringArray names;
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        names.add (d.name);
    return names;
}

juce::String MidiClockOutput::getDeviceName() const
{
    const juce::ScopedLock sl (lock);
    return deviceName;
}

void MidiClockOutput::setDeviceName (const juce::String& name, bool reopen)
{
    {
        const juce::ScopedLock sl (lock);
        if (deviceName == name)
            return;
        deviceName = name;
    }

    close();
    if (reopen)
        open();
}

bool MidiClockOutput::isOpen() const
{
    const juce::ScopedLock sl (lock);
    return output != nullptr;
}

void MidiClockOutput::open()
{
    const juce::ScopedLock sl (lock);

    const auto devices = juce::MidiOutput::getAvailableDevices();
    DBG ("[BluePrinter] MIDI: " << devices.size() << " output device(s) available");
    for (int i = 0; i < devices.size(); ++i)
        DBG ("  [" << i << "] " << devices[i].name);

    if (devices.isEmpty())
    {
        DBG ("[BluePrinter] MIDI: no output devices found — USB drum machine "
             "may need to be connected before launching the app");
        return;
    }

    int index = 0;
    if (deviceName.isNotEmpty())
    {
        for (int i = 0; i < devices.size(); ++i)
        {
            if (devices[i].name == deviceName)
            {
                index = i;
                break;
            }
        }
    }

    DBG ("[BluePrinter] MIDI: trying to open device #" << index
         << " \"" << devices[index].name << "\"");

    auto ptr = juce::MidiOutput::openDevice (devices[index].identifier);
    if (ptr == nullptr)
    {
        DBG ("[BluePrinter] MIDI: openDevice failed for \""
             << devices[index].name << "\", trying default");

        // Try the default device as a fallback
        auto def = juce::MidiOutput::getDefaultDevice();
        if (def.name.isNotEmpty())
        {
            DBG ("[BluePrinter] MIDI: default device \"" << def.name << "\"");
            ptr = juce::MidiOutput::openDevice (def.identifier);
        }
    }

    if (ptr != nullptr)
    {
        DBG ("[BluePrinter] MIDI: opened \"" << devices[index].name << "\"");
        output = std::move (ptr);
        deviceName = devices[index].name;

        // Stop any running clocks on connected gear so that the
        // next Start / clock train is clean.
        output->sendMessageNow (juce::MidiMessage::midiStop());
    }
    else
    {
        DBG ("[BluePrinter] MIDI: could not open any output device");
    }
}

void MidiClockOutput::close()
{
    const juce::ScopedLock sl (lock);
    if (output != nullptr)
    {
        DBG ("[BluePrinter] MIDI: closing device \"" << deviceName << "\"");
        output->sendMessageNow (juce::MidiMessage::midiStop());
        output.reset();
    }
}

void MidiClockOutput::sendStart()
{
    const juce::ScopedLock sl (lock);
    if (output != nullptr)
        output->sendMessageNow (juce::MidiMessage::midiStart());
}

void MidiClockOutput::sendStop()
{
    const juce::ScopedLock sl (lock);
    if (output != nullptr)
        output->sendMessageNow (juce::MidiMessage::midiStop());
}

void MidiClockOutput::sendClock()
{
    const juce::ScopedLock sl (lock);
    if (output != nullptr)
        output->sendMessageNow (juce::MidiMessage::midiClock());
}
