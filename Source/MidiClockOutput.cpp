#include "MidiClockOutput.h"

namespace
{
// Copy a MIDI message into a queue event (1..3 bytes; the queue is POD).
MidiSendQueue::Event toEvent (const juce::MidiMessage& message)
{
    MidiSendQueue::Event event;
    const int size = juce::jlimit (1, 3, message.getRawDataSize());
    for (int i = 0; i < size; ++i)
        event.data[i] = message.getRawData()[i];
    event.size = static_cast<uint8_t> (size);
    return event;
}
} // namespace

MidiClockOutput::MidiClockOutput()
    : juce::Thread ("BluePrinter MIDI clock sender")
{
    startThread (juce::Thread::Priority::normal);
}

MidiClockOutput::~MidiClockOutput()
{
    // Join the sender BEFORE the device goes away: it is the only thread
    // that calls sendMessageNow for clock pulses, so stopping it first (and
    // then closing the device on this thread) removes any chance of a send
    // into a destroyed MidiOutput.
    signalThreadShouldExit();
    notify();
    stopThread (2000);
    close();
}

void MidiClockOutput::run()
{
    while (! threadShouldExit())
    {
        // Every send*() notifies this thread, so a 20 ms idle wait costs no
        // latency; it only bounds how long a missed wake-up could sit.
        if (! drainOnce())
            wait (20);
    }
}

bool MidiClockOutput::drainOnce()
{
    const juce::ScopedLock sl (lock);
    bool didWork = false;

    // Transport first: a Start must reach the gear before the pulses that
    // follow it in the same block.
    MidiSendQueue::Event event;
    if (queue.popTransport (event))
    {
        didWork = true;

        // A Stop must be the last thing the receiver hears: drop the pulses
        // that were already queued (they belong to a train that is ending and
        // would otherwise keep the gear stepping after Stop). A Start is sent
        // first and the pulse train follows it.
        if (event.size == 1 && MidiSendQueue::transportFlushesPulses (event.data[0]))
            queue.clearPulses();

        if (output != nullptr)
            output->sendMessageNow (juce::MidiMessage (event.data, event.size));
    }

    for (int i = 0; i < maxPulsesPerDrain; ++i)
    {
        // A transport command that arrives while this batch is draining goes
        // out before any further pulse (the next pass pops it first), so a
        // late Start can never be preceded by pulses of the old train.
        if (queue.hasPendingTransport())
            break;

        if (! queue.pop (event))
            break;

        didWork = true;
        if (output != nullptr)
            output->sendMessageNow (juce::MidiMessage (event.data, event.size));
    }

    // With no device open the events above are still popped and discarded,
    // so opening the device later never flushes a stale pulse burst.
    return didWork;
}

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
    // Audio-thread safe: queue only; the sender thread delivers it.
    queue.pushTransport (toEvent (juce::MidiMessage::midiStart()));
    notify();
}

void MidiClockOutput::sendStop()
{
    queue.pushTransport (toEvent (juce::MidiMessage::midiStop()));
    notify();
}

void MidiClockOutput::sendClock()
{
    // A full queue drops the pulse rather than blocking the audio thread.
    queue.push (toEvent (juce::MidiMessage::midiClock()));
    notify();
}
