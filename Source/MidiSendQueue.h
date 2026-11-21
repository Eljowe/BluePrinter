#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

// Lock-free hand-off for direct MIDI output (clock + Start/Stop).
//
// The audio thread used to call MidiOutput::sendMessageNow() straight from
// processBlock, which took the output device's lock and blocked on the
// driver. This queue replaces that path: processBlock only enqueues a POD
// event (no lock, no allocation, no driver call) and MidiClockOutput's
// sender thread drains it and does the actual sends.
//
// Two channels, deliberately separate:
//   - Clock pulses (0xF8) go through a fixed-size SPSC ring and are dropped
//     when it is full: a missed pulse is a tiny timing wobble, and the ring
//     is huge compared with what the sender drains.
//   - Start/Stop go through a one-slot atomic mailbox, so a full pulse ring
//     can never drop a transport command. A later command overwrites an
//     earlier one: Start immediately followed by Stop collapses to Stop,
//     which is the correct net state for the receiver.
//
// The sender applies one ordering rule on top (transportFlushesPulses): when
// the popped command is Stop, the queued pulse train is discarded before the
// Stop is sent, so the receiver never keeps stepping after it stopped.
//
// Pure and allocation-free, unit-tested in Tests/test_MidiSendQueue.cpp.
class MidiSendQueue
{
public:
    // One raw MIDI message. POD, so queuing one never allocates.
    struct Event
    {
        uint8_t data[3] { 0, 0, 0 };
        uint8_t size = 0;
    };

    // Ring capacity in events. Must be a power of two (the index wrap is a
    // mask). 1024 events is ~40 s of clock at 24 ppqn; the sender drains it
    // every millisecond in practice.
    static constexpr int capacity = 1024;

    // Producer side (audio thread). Returns false when the ring is full;
    // the pulse is dropped rather than blocking.
    bool push (const Event& event) noexcept
    {
        const auto write = writeIndex.load (std::memory_order_relaxed);
        const auto read  = readIndex.load (std::memory_order_acquire);

        if (write - read >= static_cast<uint32_t> (capacity))
            return false;

        events[write & indexMask] = event;
        writeIndex.store (write + 1, std::memory_order_release);
        return true;
    }

    // Consumer side (the sender thread). Returns false when empty.
    bool pop (Event& out) noexcept
    {
        const auto read  = readIndex.load (std::memory_order_relaxed);
        const auto write = writeIndex.load (std::memory_order_acquire);

        if (read == write)
            return false;

        out = events[read & indexMask];
        readIndex.store (read + 1, std::memory_order_release);
        return true;
    }

    bool isEmpty() const noexcept
    {
        return readIndex.load (std::memory_order_acquire)
            == writeIndex.load (std::memory_order_acquire);
    }

    int getNumQueued() const noexcept
    {
        const auto write = writeIndex.load (std::memory_order_acquire);
        const auto read  = readIndex.load (std::memory_order_acquire);
        return static_cast<int> (write - read);
    }

    // Start/Stop mailbox. `Event::size` must be 1: transport messages are
    // always a single status byte, and status bytes are >= 0x80, so 0 is a
    // safe "nothing pending" sentinel.
    void pushTransport (const Event& event) noexcept
    {
        jassert (event.size == 1);
        transportStatus.store (event.data[0], std::memory_order_release);
    }

    // Pops the pending transport command, if any. At most one per call.
    bool popTransport (Event& out) noexcept
    {
        const auto status = transportStatus.exchange (0, std::memory_order_acq_rel);
        if (status == 0)
            return false;

        out = Event { { status, 0, 0 }, 1 };
        return true;
    }

    // True while a transport command is waiting. Lets the sender finish its
    // pulse batch early: the command (popped at the top of the next pass) must
    // not be overtaken by pulses.
    bool hasPendingTransport() const noexcept
    {
        return transportStatus.load (std::memory_order_acquire) != 0;
    }

    // MIDI Stop status byte.
    static constexpr uint8_t midiStopStatus = 0xFC;

    // Sender-side ordering rule for a popped transport command: a Stop must
    // be the last message the receiver hears, so the queued pulse train is
    // discarded before it is sent (otherwise gear keeps stepping after the
    // user stopped the clock). A Start is sent first and the queued pulses
    // follow it, which is the correct order for a running train.
    static constexpr bool transportFlushesPulses (uint8_t statusByte) noexcept
    {
        return statusByte == midiStopStatus;
    }

    // Consumer side only (the sender thread): drop every queued pulse.
    // Returns how many were discarded. Used for the Stop rule above.
    int clearPulses() noexcept
    {
        int dropped = 0;
        Event event;
        while (pop (event))
            ++dropped;
        return dropped;
    }

private:
    static constexpr uint32_t indexMask = static_cast<uint32_t> (capacity - 1);

    Event events[capacity] {};
    std::atomic<uint32_t> writeIndex { 0 };
    std::atomic<uint32_t> readIndex  { 0 };
    std::atomic<uint8_t>  transportStatus { 0 };
};
