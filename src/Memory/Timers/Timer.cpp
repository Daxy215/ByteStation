#include "Timer.h"

#include <algorithm>
#include <limits>

#include <GLFW/glfw3native.h>

#include "../../GPU/Gpu.h"
#include "../IRQ.h"

/**
 * Caution: Reading the Current Counter Value can be a little unstable (when using dotclk or hblank as clock source);
 * the GPU clock isn't in sync with the CPU clock, so the timer may get changed during the CPU read cycle.
 * As a workaround: repeat reading the timer until the received value is the same (or slightly bigger) than the previous value.
 */

Emulator::IO::Timer::Timer(TimerType type) : type(type) {
	
}

void Emulator::IO::Timer::step(uint32_t cpuCycles, uint32_t dotTicks) {
	// Synchronization
	if (mode.sync) {
		switch (type) {
			case TimerType::Timer0_DotClock:
				switch (mode.syncMode) {
					case 0: // Pause during HBlank
						if (isInHBlank) return;
						break;

					case 1: // Reset at HBlank
				        if (wasInHBlank && !isInHBlank) {
							counter = 0;
							_cycles = 0;
						    resetPending = false;
							return;
						}

						break;

					case 2: // Reset at HBlank, pause outside
				        if (!wasInHBlank && isInHBlank) {
							counter = 0;
							_cycles = 0;
						    resetPending = false;
						}

				        if (!isInHBlank)
				            return;

						break;

					case 3: // Pause until next HBlank edge, then free run
				        if (wasInHBlank && !isInHBlank) {
							mode.sync = 0; // disable sync
						} else {
							return;
						}

						break;
                    default:
				        printf("WTF??\n");
				        break;
                }

				break;

			case TimerType::Timer1_HBlank:
				switch (mode.syncMode) {
					case 0: // Pause during VBlank
						if (isInVBlank) return;
						break;

					case 1: // Reset at VBlank
				        if (wasInVBlank && !isInVBlank) {
							counter = 0;
							_cycles = 0;
				            resetPending = false;
							return;
						}

						break;

					case 2: // Reset at VBlank, pause outside
				        if (!wasInVBlank && isInVBlank) {
							counter = 0;
							_cycles = 0;
				            resetPending = false;
						}

				        if (!isInVBlank)
				            return;

						break;

					case 3: // Pause until VBlank edge, then free-run
				        if (wasInVBlank && !isInVBlank) {
							mode.sync = 0;
						} else {
							return;
						}

						break;
			        default:
				        break;
				}

				break;

			case TimerType::Timer2_SystemClock8:
				// syncMode 0 and 3 both pause timer
				if (mode.syncMode == 0 || mode.syncMode == 3) {
					_cycles = 0;
					return;
				}

				break;
		}
	}
    
    if (type == TimerType::Timer0_DotClock && (mode.source & 1)) {
        finishTick(dotTicks);
        return;
    }

    _cycles += cpuCycles;

	// Clock source
	switch (type) {
		case TimerType::Timer0_DotClock: {
		    // System clock
		    finishTick(_cycles);
		    _cycles = 0;

	        break;
		}

		case TimerType::Timer1_HBlank: {
		    if (mode.source & 1) {
		        // Count rising edge only
		        if (!wasInHBlank && isInHBlank)
		            finishTick(1);

		        _cycles = 0;
		    } else {
		        // System clock
		        finishTick(_cycles);
		        _cycles = 0;
		    }

		    break;
		}

		case TimerType::Timer2_SystemClock8: {
		    if (mode.source >= 2) {
		        // system clock / 8
		        finishTick(_cycles / 8);
		        _cycles %= 8;
		    } else {
		        // normal system clock
		        finishTick(_cycles);
		        _cycles = 0;
		    }

		    break;
		}
	}
}

void Emulator::IO::Timer::finishTick(uint32_t ticks) {
    while (ticks--) {
        if (resetPending) {
            counter = 0;
            resetPending = false;
            continue;
        }

        counter++;

        bool hitTarget = (target == 0) || (counter == target);

        if (hitTarget) {
            mode.reachedTarget = true;

            if (mode.interruptTarget)
                requestIRQ();

            if (mode.resetType && target != 0)
                resetPending = true;
        }

        if (counter > 0xffff) {
            mode.hasWrapped = true;

            if (mode.interruptWrap)
                requestIRQ();

            counter = 0;
        }
    }
}

/*
void Emulator::IO::Timer::finishTick(uint32_t ticks) {
    while (ticks--) {
        if (resetPending) {
            counter = 0;
            resetPending = false;

            continue;
        }

        //counter = (counter + 1) & 0xffff;
        counter++;

        if (counter == target) {
            mode.reachedTarget = true;

            if (mode.interruptTarget)
                requestIRQ();

            if (mode.resetType) {
                counter = 0;
                resetPending = false;
                continue;
            }
        }

        if (counter > 0xffff) {
            mode.hasWrapped = true;
            counter = 0;

            if (mode.interruptWrap)
                requestIRQ();

            if (!mode.resetType) {
                resetPending = false;
            }
        }
    }
}
*/

void Emulator::IO::Timer::requestIRQ() {
	if (mode.interruptMode == 0) {
		// One-shot: only fire if not already fired
		if (!mode.interrupt) return;
		mode.interrupt = false;
	} else {
		// Toggle mode or pulse mode
		if (mode.interruptToggleMode)
			mode.interrupt = !mode.interrupt;
		else
			mode.interrupt = true; // had this as false.. ;-;
	}
	
	switch (type) {
		case TimerType::Timer0_DotClock:
			IRQ::trigger(IRQ::Timer0);
			break;
		case TimerType::Timer1_HBlank:
			IRQ::trigger(IRQ::Timer1);
			break;
		case TimerType::Timer2_SystemClock8:
			IRQ::trigger(IRQ::Timer2);
			break;
	}
}

void Emulator::IO::Timer::syncGpu(bool isInHBlank, bool isInVBlank, uint8_t dotClockDivisor) {
	this->wasInHBlank = this->isInHBlank;
	this->wasInVBlank = this->isInVBlank;
	
	this->isInHBlank = isInHBlank;
	this->isInVBlank = isInVBlank;
    this->dotClockDivisor = dotClockDivisor;
}

void Emulator::IO::Timer::setMode(uint16_t val) {
	/**
	 * 0     Synchronization Enable (0=Free Run, 1=Synchronize via Bit1-2)
     * 1-2   Synchronization Mode   (0-3, see lists below)
     * 			Synchronization Modes for Counter 0:
     * 		 		0 = Pause counter during Hblank(s)
     * 		 		1 = Reset counter to 0000h at Hblank(s)
     * 		 		2 = Reset counter to 0000h at Hblank(s) and pause outside of Hblank
     * 		 		3 = Pause until Hblank occurs once, then switch to Free Run
     * 
     * 		 Synchronization Modes for Counter 1:
     * 			Same as above, but using Vblank instead of Hblank
     * 
     * 		 Synchronization Modes for Counter 2:
     * 		 	0 or 3 = Stop counter at current value (forever, no h/v-blank start)
     * 		 	1 or 2 = Free Run (same as when Synchronization Disabled)
     * 
     * 3     Reset counter to 0000h  (0=After Counter=FFFFh, 1=After Counter=Target)
     * 4     IRQ when Counter=Target (0=Disable, 1=Enable)
     * 5     IRQ when Counter=FFFFh  (0=Disable, 1=Enable)
     * 6     IRQ Once/Repeat Mode    (0=One-shot, 1=Repeatedly)
     * 7     IRQ Pulse/Toggle Mode   (0=Short Bit10=0 Pulse, 1=Toggle Bit10 on/off)
     * 
     * 8-9   Clock Source (0-3, see list below)
     * 		 	Counter 0:  0 or 2 = System Clock,  1 or 3 = Dotclock
     * 		 	Counter 1:  0 or 2 = System Clock,  1 or 3 = Hblank
     * 		 	Counter 2:  0 or 1 = System Clock,  2 or 3 = System Clock/8
     * 
     * 10    Interrupt Request       (0=Yes, 1=No) (Set after Writing)    (W=1) (R)
     * 11    Reached Target Value    (0=No, 1=Yes) (Reset after Reading)        (R)
     * 12    Reached FFFFh Value     (0=No, 1=Yes) (Reset after Reading)        (R)
	 */
	
	//mode._reg = val;
	
	// Reset values after writing
 	//mode.interrupt = true;
	//wasIRQ = false;
    //resetPending = false;

	//counter = 0;
	//holdAtZero = 2;

    mode._reg = static_cast<uint16_t>(val & 0x03FF);

    mode.interrupt = true;
    mode.reachedTarget = false;
    mode.hasWrapped = false;

    wasIRQ = false;
    resetPending = false;

    counter = 0;
    _cycles = 0;
}

uint16_t Emulator::IO::Timer::getMode() {
    const auto f = mode._reg;
	
    mode.reachedTarget = false;
    mode.hasWrapped = false;
	
	return f;
}

uint32_t Emulator::IO::Timer::cyclesUntilNextEvent() const {
	if (mode.sync) {
		switch (type) {
			case TimerType::Timer0_DotClock:
				if (mode.syncMode == 0 && isInHBlank) return std::numeric_limits<uint32_t>::max();
				if (mode.syncMode == 2 && !isInHBlank) return std::numeric_limits<uint32_t>::max();
				break;

			case TimerType::Timer1_HBlank:
				if (mode.syncMode == 0 && isInVBlank) return std::numeric_limits<uint32_t>::max();
				if (mode.syncMode == 2 && !isInVBlank) return std::numeric_limits<uint32_t>::max();
				break;

			case TimerType::Timer2_SystemClock8:
				if (mode.syncMode == 0 || mode.syncMode == 3) return std::numeric_limits<uint32_t>::max();
				break;
		}
	}

	bool gpuClockedSource = (type == TimerType::Timer0_DotClock || type == TimerType::Timer1_HBlank) && (mode.source & 1);
	if (gpuClockedSource)
		return std::numeric_limits<uint32_t>::max();

	if (!mode.interruptTarget && !mode.interruptWrap)
		return std::numeric_limits<uint32_t>::max();

	uint32_t ticksToTarget = std::numeric_limits<uint32_t>::max();
	if (mode.interruptTarget)
		ticksToTarget = target == 0 ? 1 : (counter < target ? target - counter : (0x10000 - counter) + target);

	uint32_t ticksToWrap = std::numeric_limits<uint32_t>::max();
	if (mode.interruptWrap)
		ticksToWrap = 0x10000 - counter;

	uint32_t ticksNeeded = std::min(ticksToTarget, ticksToWrap);

	if (type == TimerType::Timer2_SystemClock8 && mode.source >= 2)
		return (8 - _cycles) + (ticksNeeded - 1) * 8;

	return ticksNeeded;
}

void Emulator::IO::Timer::reset() {
	_cycles = 0;
	counter = 0;
	target = 0;
	mode = {};

	/*isInHBlank = false;
	isInVBlank = false;
	
	wasInHBlank = false;
	wasInVBlank = false;*//**/
	
	wasIRQ = false;
    resetPending = false;

	// Keep type :}
	//_type = {};
}
