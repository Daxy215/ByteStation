#include "Dma.h"

#include <iostream>
#include <limits>

#include "../Memory/IRQ.h"

void Dma::step() {
    if(interruptPending) {
        interruptPending = false;
        IRQ::trigger(IRQ::Interrupt::Dma);
    }
}

uint32_t Dma::cyclesUntilNextEvent() const {
    return interruptPending ? 1 : std::numeric_limits<uint32_t>::max();
}

uint32_t Dma::interrupt() {
    uint32_t r = 0;
    
    r |= (static_cast<uint32_t>(irqDummy));
    r |= (static_cast<uint32_t>(forceIrq)) << 15;
    r |= (static_cast<uint32_t>(channelIrqEn)) << 16;
    r |= (static_cast<uint32_t>(irqEn)) << 23;
    r |= (static_cast<uint32_t>(channelIrqFlags)) << 24;
    r |= (static_cast<uint32_t>(irqFlag)) << 31;
    
    return r;
}

void Dma::setInterrupt(uint32_t val) {
    // Unknown what bts [14:0] do
    irqDummy = static_cast<uint8_t>(val & 0x7FFF);
    
    forceIrq = (val >> 15) & 1;
    
    channelIrqEn = (val >> 16) & 0x7F;
    
    irqEn = (val >> 23) & 1;

    // Writing 1 to a flag rests it
    channelIrqFlags &= ~((val >> 24) & 0x7f);

    updateInterruptPending();
}

bool Dma::irq() {
    return forceIrq || (this->irqEn && (this->channelIrqFlags != 0));
}

void Dma::setControl(uint32_t val) {
    control = val;
}

void Dma::reset() {
    irqEn = false;
    channelIrqEn = 0;
    channelIrqFlags = 0;
    
    forceIrq = false;
    interruptPending = false;
    
    irqDummy = 0;
    irqFlag = false;
    
    /**
     *  Initial value on reset is 07654321h.
     *  If two or more channels have the same priority setting,
     *  then the priority is determined by the channel number (DMA0=Lowest, DMA6=Highest, CPU=higher than DMA6?).
     */
    control = 0x07654321;
    
    for(auto& channel : channels)
        channel.reset();
}
