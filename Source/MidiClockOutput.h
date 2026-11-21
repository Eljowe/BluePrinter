#pragma once

#include <JuceHeader.h>
#include <memory>

#include "MidiSendQueue.h"

// The standalone's direct MIDI output for the MIDI clock and Start/Stop.
//
// BluePrinter never forwards the host MIDI buffer to hardware (the host may
// not deliver it), so when the clock enables Start/Stop/0xF8 are sent
// straight to the selected device here.
//
// Threading: the device open/close and the actual sends run on the message
// thread and on this object's own sender thread. The audio thread only ever
// enqueues into the lock-free MidiSendQueue (processBlock must not take a
// lock or block on a MIDI driver); the sender thread is notified by every
// send and drains the queue with sendMessageNow.
class MidiClockOutput : private juce::Thread
{
public:
    MidiClockOutput();
    ~MidiClockOutput() override;

    // Names of the available MIDI output devices, in device order.
    static juce::StringArray getAvailableDeviceNames();

    // Message thread. Changes the selected device. When `reopen` is true
    // and the name actually changed, closes and re-opens immediately (the
    // clock is running); otherwise it just records the name for the next
    // open() (state restore).
    void setDeviceName (const juce::String& name, bool reopen);

    juce::String getDeviceName() const;

    // Message thread. Opens the selected device, falling back to the
    // default device when the selection can't be opened. Safe to call when
    // already open (it replaces the open device).
    void open();

    // Message thread. Sends Stop and closes the device.
    void close();

    bool isOpen() const;

    // Queued direct sends. Safe from the audio thread: they only push into
    // the lock-free queue (no lock, no driver call, no allocation).
    void sendStart();
    void sendStop();
    void sendClock();

private:
    // Sender thread: drains the queue and performs the real sends.
    void run() override;

    // One drain pass: transport command first, then up to
    // maxPulsesPerDrain clock pulses. Returns true when it popped anything.
    bool drainOnce();

    // Bounded so a long queue can't hold the device lock (and so a clock
    // burst is spread over a few passes).
    static constexpr int maxPulsesPerDrain = 64;

    mutable juce::CriticalSection lock;
    juce::String deviceName;
    std::unique_ptr<juce::MidiOutput> output;

    // Audio-thread -> sender-thread hand-off (see MidiSendQueue.h).
    MidiSendQueue queue;
};
