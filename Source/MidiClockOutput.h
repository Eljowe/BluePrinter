#pragma once

#include <JuceHeader.h>
#include <memory>

// The standalone's direct MIDI output for the MIDI clock and Start/Stop.
//
// BluePrinter never forwards the host MIDI buffer to hardware (the host may
// not deliver it), so when the clock enables Start/Stop/0xF8 are sent
// straight to the selected device here. The device open/close must run on
// the message thread; the sends are guarded so the audio thread can clock
// out safely.
class MidiClockOutput
{
public:
    MidiClockOutput() = default;

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

    // Thread-safe direct sends to connected hardware.
    void sendStart();
    void sendStop();
    void sendClock();

private:
    mutable juce::CriticalSection lock;
    juce::String deviceName;
    std::unique_ptr<juce::MidiOutput> output;
};
