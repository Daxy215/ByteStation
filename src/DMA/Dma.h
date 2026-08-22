#pragma once

#include "Channel.h"

class Dma {
    public:
        Dma() = default;

        void step();

        // Helper functions
        // Retrieve the value of the interrupt register
        uint32_t interrupt();

        // Set the value of the interrupt register
        void setInterrupt(uint32_t val);

        // Returns the status of the DMA interrupt
        bool irq();

        void setControl(uint32_t val);

        void updateInterruptPending() {
            bool prevIrq = irqFlag;

            irqFlag = irq();

            if(!prevIrq && irqFlag) {
                interruptPending = true;
            }
        }

        // Returns a reference to a channel by the port number
        //Channel& getChannel(Port port) const { return channels[static_cast<size_t>(port)]; }

        // Returns a reference to a channel by port number
        Channel& getChannel(Port port) { return channels[static_cast<size_t>(port)]; }

        bool channelEnabled(Port port) const {
            uint32_t index = static_cast<uint32_t>(port);
            return (control & (1u << (index * 4 + 3))) != 0;
        }

        void reset();

        uint32_t cyclesUntilNextEvent() const;

    public:
        // master IRQ enable
        bool irqEn = false;

        // IRQ enable for individual channels
        uint8_t channelIrqEn = 0;

        // IRQ flags for individual channels
        uint8_t channelIrqFlags = 0;

        // When set the interrupt is active unconditionally (even if 'irq_en' is false)
        bool forceIrq = false;
        bool interruptPending = false;

        // Bits [0:5] of the interrupt registers are RW but I don't know,
        // what they're supposed to do so I just store them and send them,
        // back untouched on reads
        uint16_t irqDummy = 0;

        bool irqFlag = false;

        // Rest value taken from the Nocash PSX spec
        uint32_t control = 0x07654321;

    private:
        // The 7 channel instances
        Channel channels[7];
};
