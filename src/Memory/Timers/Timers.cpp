#include "Timers.h"

#include <algorithm>
#include <cstdio>
#include <limits>

Emulator::IO::Timers::Timers()
	: timers{ new Timer(TimerType::Timer0_DotClock), new Timer(TimerType::Timer1_HBlank), new Timer(TimerType::Timer2_SystemClock8) } {
	
}

void Emulator::IO::Timers::step(uint32_t cycles, uint32_t dotTicks) {
    timers[0]->step(cycles, dotTicks);
    timers[1]->step(cycles, 0);
    timers[2]->step(cycles, 0);
}

void Emulator::IO::Timers::sync(bool isInHBlank, bool isInVBlank, uint8_t dotClockDivisor) {
	timers[0]->syncGpu(isInHBlank, isInVBlank, dotClockDivisor);
	timers[1]->syncGpu(isInHBlank, isInVBlank, dotClockDivisor);
	
	// Timer 3 only uses system clock
}

uint32_t Emulator::IO::Timers::load(uint32_t addr) {
	auto index = addr >> 4;
	
	Timer& timer = *timers[index];
	
	switch (addr & 0xF) {
		case 0: {
			// Counter (R/W)

		    /**
		     * Unsure why but freebios seems to wait till timer2.counter > 0x00004E84
		     */
		    //if (index == 1/* && timer.counter > 0x00004E84*/) {
                //printf("ayo\n");
		    //    return static_cast<uint16_t>((timer.counter + 0x00004E84));
		   // }

			return static_cast<uint16_t>((timer.counter));
		}
		
		case 4: {
			// Mode (R/W)
			return timer.getMode();
		}
		
		case 8: {
			// Target value (R/W)
			return timer.target;
		}
	}
	
	return 0;
}

void Emulator::IO::Timers::store(uint32_t addr, uint32_t val) {
	auto index = addr >> 4;
	
	Timer& timer = *timers[index];

    //if (val == 0xFFFF) {
    //    printf("f");
    //}

	switch (addr & 0xF) {
		case 0: {
			// Counter (R/W)
		    timer.counter = static_cast<uint16_t>(val);
		    timer._cycles = 0;
		    timer.resetPending = false;
			
			//if (timer.counter > timer.target) {
			//	timer.ignoreTargetUntilWrap = true;
			//}
			
			break;
		}
		
		case 4: {
			// Mode (R/W)
			timer.setMode(static_cast<uint16_t>(val));
			
			break;
		}
		
		case 8: {
			// Target value (R/W)
			timer.target = static_cast<uint16_t>(val);
			
			break;
		}
	}
}

uint32_t Emulator::IO::Timers::cyclesUntilNextEvent() const {
	uint32_t next = std::numeric_limits<uint32_t>::max();

	for(auto* timer : timers)
		next = std::min(next, timer->cyclesUntilNextEvent());

	return next;
}

void Emulator::IO::Timers::reset() {
	for(auto& timer : timers)
		timer->reset();
}
