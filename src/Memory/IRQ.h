#pragma once

#include "../CPU/COP/COP0.h"

class IRQ {
    public:
	    enum Interrupt {
		    VBlank = 0,
		    GPU = 1, // Rarely used, triggered by GP0(1F)
		    CDROM = 2,
		    Dma = 3,
		    Timer0 = 4, // Root Counter 0 (Sysclk or Dotclk)
		    Timer1 = 5, // Root Counter 1 (Sysclk or H-blank)
		    Timer2 = 6, // Root Counter 2 (Sysclk or Sysclk /8)
		    PadMemCard = 7, // Byte Received Interrupt
		    SIO = 8, // ??
		    SPU = 9,
		    LightPen = 10, // Lightpen interrupt. Also shared by PIO and DTL cards
	    };

	    IRQ() = default;

	    static void step() {
	        if ((status & mask & 0x07FF) != 0)
	            COP0::cause |= 0x400;
	        else
	            COP0::cause &= ~0x400;
	    }

	    static uint16_t getStatus() {
		    return status;
	    }

	    static void acknowledge(uint16_t ack) noexcept {
	        status &= ack;
	        status &= 0x07FF;
	        step();
	    }

	    static uint16_t getMask() {
		    return mask;
	    }

	    static void setMask(uint16_t val) {
	        mask = val & 0x07FF;
	        step();
	    }

	    static uint32_t active() {
	        return (status & mask & 0x07FF) != 0;
	    }

	    static void trigger(Interrupt interrupt) {
	        status |= static_cast<uint16_t>(1u << static_cast<uint16_t>(interrupt));
	        status &= 0x07FF;
	        step();
	    }

	    static void reset() {
	        status = 0;
	        mask = 0;
	        step();
	    }

    public:
	    // Im too lazy
	    inline static uint16_t status = 0;
	    inline static uint16_t mask = 0;
};

