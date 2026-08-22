#pragma once
#include <stdint.h>

#include "Timer.h"

namespace Emulator {
	namespace IO {
		class Timers {
		public:
			Timers();
			
			void step(uint32_t cycles, uint32_t dotTicks);
			void sync(bool isInHBlank, bool isInVBlank, uint8_t dotClockDivisor);
			
			uint32_t load(uint32_t addr);
			void store(uint32_t addr, uint32_t val);
			
			void reset();
			
			uint32_t cyclesUntilNextEvent() const;
			
		public:
			/**
			 * There are 3 different timers,
			 *
			 * Counter 0: GPU dotclock
			 * Counter 1: GPU h-blanking
			 * Counter 3: System clock / 8
			 */
			Timer* timers[3];
		};
	}
}
