#include "TestRunner.h"
#include "MidiSendQueue.h"

// Lock-free MIDI send queue: the audio thread only enqueues, the sender
// thread drains (see MidiSendQueue.h).

namespace
{
MidiSendQueue::Event bytes (uint8_t a, uint8_t b = 0, uint8_t c = 0, uint8_t size = 1)
{
    return MidiSendQueue::Event { { a, b, c }, size };
}

uint8_t firstByte (const MidiSendQueue::Event& e)
{
    return e.size > 0 ? e.data[0] : 0;
}
} // namespace

BP_TEST (MidiSendQueue_preservesOrder)
{
    MidiSendQueue queue;

    BP_CHECK (queue.isEmpty());
    BP_CHECK (queue.push (bytes (0xF8)));
    BP_CHECK (queue.push (bytes (0x90, 60, 100, 3)));
    BP_CHECK (queue.push (bytes (0xF8)));

    BP_CHECK_EQ (queue.getNumQueued(), 3);

    MidiSendQueue::Event e;
    BP_CHECK (queue.pop (e));
    BP_CHECK_EQ (static_cast<int> (e.size), 1);
    BP_CHECK_EQ (static_cast<int> (e.data[0]), 0xF8);

    BP_CHECK (queue.pop (e));
    BP_CHECK_EQ (static_cast<int> (e.size), 3);
    BP_CHECK_EQ (static_cast<int> (e.data[0]), 0x90);
    BP_CHECK_EQ (static_cast<int> (e.data[1]), 60);
    BP_CHECK_EQ (static_cast<int> (e.data[2]), 100);

    BP_CHECK (queue.pop (e));
    BP_CHECK_EQ (static_cast<int> (e.data[0]), 0xF8);

    BP_CHECK (! queue.pop (e));
    BP_CHECK (queue.isEmpty());
}

BP_TEST (MidiSendQueue_wrapsAroundAndKeepsOrder)
{
    MidiSendQueue queue;

    // Cycle more events than the ring holds so the indices wrap several
    // times; the sequence must still come out in order.
    int next = 0;
    int received = 0;

    for (int round = 0; round < MidiSendQueue::capacity * 3; ++round)
    {
        // Keep the ring half full so push never drops.
        while (queue.getNumQueued() >= MidiSendQueue::capacity / 2)
        {
            MidiSendQueue::Event e;
            BP_CHECK (queue.pop (e));
            BP_CHECK_EQ (static_cast<int> (e.data[1]), received & 0x7F);
            ++received;
        }

        BP_CHECK (queue.push (bytes (0xF8, static_cast<uint8_t> (next & 0x7F))));
        ++next;
    }

    BP_CHECK_EQ (next, received + queue.getNumQueued());
}

BP_TEST (MidiSendQueue_dropsWhenFull)
{
    MidiSendQueue queue;

    for (int i = 0; i < MidiSendQueue::capacity; ++i)
        BP_CHECK (queue.push (bytes (0xF8, static_cast<uint8_t> (i & 0x7F))));

    BP_CHECK_EQ (queue.getNumQueued(), MidiSendQueue::capacity);

    // Full: the pulse is dropped, not queued and not blocking.
    BP_CHECK (! queue.push (bytes (0xF8)));
    BP_CHECK_EQ (queue.getNumQueued(), MidiSendQueue::capacity);

    // Draining one slot makes room again.
    MidiSendQueue::Event e;
    BP_CHECK (queue.pop (e));
    BP_CHECK (queue.push (bytes (0xF8)));
    BP_CHECK_EQ (queue.getNumQueued(), MidiSendQueue::capacity);
}

BP_TEST (MidiSendQueue_transportIsNotLostWhenFull)
{
    MidiSendQueue queue;

    for (int i = 0; i < MidiSendQueue::capacity; ++i)
        queue.push (bytes (0xF8));

    BP_CHECK (! queue.push (bytes (0xF8)));  // ring is full

    // Start rides the separate mailbox, so it survives a full pulse ring.
    queue.pushTransport (bytes (0xFA));

    MidiSendQueue::Event e;
    BP_CHECK (queue.popTransport (e));
    BP_CHECK_EQ (static_cast<int> (firstByte (e)), 0xFA);
    BP_CHECK_EQ (static_cast<int> (e.size), 1);

    // Consumed exactly once.
    BP_CHECK (! queue.popTransport (e));
}

BP_TEST (MidiSendQueue_transportLaterCommandWins)
{
    MidiSendQueue queue;

    // Start immediately followed by Stop (within one drain interval) must
    // leave the receiver stopped, not started.
    queue.pushTransport (bytes (0xFA));
    queue.pushTransport (bytes (0xFC));

    MidiSendQueue::Event e;
    BP_CHECK (queue.popTransport (e));
    BP_CHECK_EQ (static_cast<int> (firstByte (e)), 0xFC);
    BP_CHECK (! queue.popTransport (e));
}

BP_TEST (MidiSendQueue_stopDiscardsQueuedPulses)
{
    MidiSendQueue queue;

    // The sender's ordering rule: a Stop must be the last thing the receiver
    // hears, so the pulses queued before it are dropped first.
    for (int i = 0; i < 8; ++i)
        queue.push (bytes (0xF8));
    queue.pushTransport (bytes (0xFC));

    MidiSendQueue::Event transport;
    BP_CHECK (queue.popTransport (transport));
    BP_CHECK_EQ (static_cast<int> (firstByte (transport)), 0xFC);

    BP_CHECK (MidiSendQueue::transportFlushesPulses (firstByte (transport)));
    BP_CHECK_EQ (queue.clearPulses(), 8);
    BP_CHECK (queue.isEmpty());
    BP_CHECK_EQ (queue.clearPulses(), 0);
}

BP_TEST (MidiSendQueue_startKeepsQueuedPulses)
{
    MidiSendQueue queue;

    // A Start is sent before the train, so its queued pulses must survive.
    queue.pushTransport (bytes (0xFA));
    queue.push (bytes (0xF8));
    queue.push (bytes (0xF8));

    BP_CHECK (queue.hasPendingTransport());

    MidiSendQueue::Event transport;
    BP_CHECK (queue.popTransport (transport));
    BP_CHECK_EQ (static_cast<int> (firstByte (transport)), 0xFA);
    BP_CHECK (! queue.hasPendingTransport());
    BP_CHECK (! MidiSendQueue::transportFlushesPulses (firstByte (transport)));
    BP_CHECK_EQ (queue.getNumQueued(), 2);

    MidiSendQueue::Event e;
    BP_CHECK (queue.pop (e));
    BP_CHECK_EQ (static_cast<int> (firstByte (e)), 0xF8);
}
