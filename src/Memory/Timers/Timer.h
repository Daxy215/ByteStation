#pragma once
#include <stdint.h>

namespace Emulator {
	class Gpu;
}

namespace Emulator {
	namespace IO {
		enum class TimerType {
			Timer0_DotClock,
			Timer1_HBlank,
			Timer2_SystemClock8,
		};
		
		enum class Clock {
			SystemClock,
			SystemClock8,
			DotClock,
			HBlank,
		};
		
		union Mode {
			struct {
				uint16_t sync      : 1;
				uint16_t syncMode  : 2;
				uint16_t resetType : 1;
				
				uint16_t interruptTarget     : 1;
				uint16_t interruptWrap       : 1;
				uint16_t interruptMode       : 1;
				uint16_t interruptToggleMode : 1;
				
				// For all timer different clock sources are available
			    /**
                 *  For Counter 0: 0 or 2 = System Clock, 1 or 3 = Dotclock<br>
                 *  For Counter 1: 0 or 2 = System Clock, 1 or 3 = HBlank<br>
                 *  For Counter 2: 0 or 1 = System Clock, 2 or 3 = System Clock / 8<br>
                 */
				uint16_t source        : 2;
				uint16_t interrupt     : 1; // R
				uint16_t reachedTarget : 1; // R
				uint16_t hasWrapped    : 1; // R
				
				uint16_t : 3;
			};
			
			uint16_t _reg;
			
			Mode() : _reg(0) { interrupt = 1; }
			Mode(uint16_t reg) : _reg(reg) {}
		};
		
		class Timer {
			public:
				Timer(TimerType type);
				
				void step(uint32_t cycles,uint32_t dotTicks);
				void syncGpu(bool isInHBlank, bool isInVBlank, uint8_t dotClockDivisor);
				
				void setMode(uint16_t val);
				uint16_t getMode();
				
				void reset();

				uint32_t cyclesUntilNextEvent() const;
				
			private:
				void finishTick(uint32_t cycles);
				void requestIRQ();

			public:
				uint32_t _cycles = 0;
				uint32_t dotCycleAcc = 0;
				uint32_t dotClockDivisor = 0;
				uint32_t sys8Acc = 0;
				
				//uint32_t t = 0;
				uint32_t counter = 0;
				uint16_t target = 0;
				
				/*// 0 = Free Run, 1 = Sync via Bit1-2
				bool sync = false;
				
				/**
				 * For timer 0 (GPU dotclock)
					 * 0 = Pause counter during Hblank(s)
					 * 1 = Reset counter to 0000h at HBlank
					 * 2 = Reset counter to 0000h at HBlank and pause outside HBlank
					 * 3 = Pause until HBlank occurs once, then switch to Free Run (turn off sync)
					 
				 * For timer 1 (GPU hBlank)
					* Same as timer 0 but uses VBlank instead of HBlank
					
				 * For timer 2 (System clock / 8)
					* 0 or 3 = Stop counter at current value (forever, no h/v-blank start)
					* 1 or 2 = Free Run (Same as when Sync is disabled)
				 #1#
				uint8_t syncMode = 0;
				
				/**
				 * Reset after:
					 * 0 = Counter reaches FFFFh
					 * 1 = Counter reaches Target
				 #1#
				bool resetType = false;
				
				/**
				 * Causes an interrupt if,
				 * counter reaches target
				 #1#
				bool interruptTarget = false;
				
				/**
				 * Causes an interrupt if,
				 * counter reaches FFFFh
				 #1#
				bool interruptWrap = false;
				
				/**
				 * 0 = One-shot
				 * 1 = Repeatedly
				 #1#
				bool interruptMode = false;
				
				/**
				 * 0 = Short Bit 10 = 0 Pulse
				 * 1 = Toggle Bit 10 on/off
				 #1#
				bool interruptToggleMode = false;
				
				/**
				 *  For Counter 0: 0 or 2 = System Clock, 1 or 3 = Dotclock
				 *  For Counter 1: 0 or 2 = System Clock, 1 or 3 = HBlank
				 *  For Counter 2: 0 or 1 = System Clock, 2 or 3 = System Clock / 8
				 #1#
				uint8_t source = 0;
				
				/**
				 * 0 = Yes
				 * 1 = No
				 * (Set after Writing)
				 #1#
				bool interrupt = false;
				
				/**
				 * 0 = No
				 * 1 = Yes
				 * (Reset after Reading)
				 #1#
				bool reachedTarget = false;
				bool hasWrapped = false;*/
				
				//bool paused = false;
				
				Mode mode;
				
			public:
				bool isInHBlank = false;
				bool isInVBlank = false;
				
				bool wasInHBlank = false;
				bool wasInVBlank = false;

                bool resetPending = false;
				
				bool wasIRQ = false;
				
			private:
				TimerType type;
		};
	}
}
