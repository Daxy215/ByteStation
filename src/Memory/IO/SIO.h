#pragma once
#include <array>
#include <cstdio>
#include <stdint.h>
#include <GLFW/glfw3.h>

#include "Peripherals/DigitalController.h"
#include "Peripherals/MemoryCard.h"

class GLFWwindow;

namespace Emulator {
	namespace IO {
		class SIO {
			enum ConnectedDevice {
				None,
				Controller,
				MemoryCard
			};
			
			union STAT {
				/**
				 * SIO#_STAT (R)
				 * 
				 * 0     TX FIFO Not Full       (1=Ready for new byte)  (depends on CTS) (TX requires CTS)
				 * 1     RX FIFO Not Empty      (0=Empty, 1=Data available)
				 * 2     TX Idle                (1=Idle/Finished)       (depends on TXEN and on CTS)
				 * 3     RX Parity Error        (0=No, 1=Error; Wrong Parity, when enabled) (sticky)
				 * 4     SIO1 RX FIFO Overrun   (0=No, 1=Error; received more than 8 bytes) (sticky)
				 * 5     SIO1 RX Bad Stop Bit   (0=No, 1=Error; Bad Stop Bit) (when RXEN)   (sticky)
				 * 6     SIO1 RX Input Level    (0=Normal, 1=Inverted) ;only AFTER receiving Stop Bit
				 * 7     DSR Input Level        (0=Off, 1=On) (remote DTR) ;DSR not required to be on
				 * 8     SIO1 CTS Input Level   (0=Off, 1=On) (remote RTS) ;CTS required for TX
				 * 9     Interrupt Request      (0=None, 1=IRQ) (See SIO_CTRL.Bit4,10-12)   (sticky)
				 * 10    Unknown                (always zero)
				 * 11-31 Baudrate Timer         (15-21 bit timer, decrementing at 33MHz)
				 */
				struct {
					uint32_t TX_FIFO_NOT_FULL       : 1; // (1=Ready for new byte)  (depends on CTS) (TX requires CTS)
					uint32_t RX_FIFO_NOT_EMPTY      : 1; // (0=Empty, 1=Data available)
					uint32_t TX_IDLE                : 1; // (1=Idle/Finished)       (depends on TXEN and on CTS)
					uint32_t RX_PARITY_ERROR		: 1; // (0=No, 1=Error; Wrong Parity, when enabled) (sticky)
					uint32_t SIO1_RX_FIFO_OVERRUN   : 1; // (0=No, 1=Error; received more than 8 bytes) (sticky)
					uint32_t SIO1_RX_BAD_STOP_BIT   : 1; // (0=No, 1=Error; Bad Stop Bit) (when RXEN)   (sticky)
					uint32_t SIO1_RX_INPUT_LEVEL    : 1; // (0=Normal, 1=Inverted) ;only AFTER receiving Stop Bit
					uint32_t DSR_INPUT_LEVEL        : 1; // (0=Off, 1=On) (remote DTR) ;DSR not required to be on
					uint32_t SIO1_CTS_INPUT_LEVEL   : 1; // (0=Off, 1=On) (remote RTS) ;CTS required for TX
					uint32_t INTERRUPT_REQUEST      : 1; // (0=None, 1=IRQ) (See SIO_CTRL.Bit4,10-12)   (sticky)
					uint32_t UNKNOWN                : 1; // ALWAYS ZERO
					uint32_t BAUDRATE_TIMER         : 21; // (15-21 bit timer, decrementing at 33MHz)
				};
				
				uint32_t reg = 0;
				
				explicit STAT(const uint32_t reg) : reg(reg) {}
			} __attribute__((packed));
			
			public:
				void step(uint32_t cycles);
				
				uint32_t load(uint32_t addr, uint32_t size);
				void store(uint32_t addr, uint32_t val, uint32_t size);
				
				void setCtrl(uint32_t port, uint32_t val);

				uint32_t cyclesUntilNextEvent() const;
				
				static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
					if (action == GLFW_PRESS || action == GLFW_RELEASE) {
						bool isPressed = (action == GLFW_PRESS);
						
						// TODO; Handle 2nd controller input
						auto& controller = _controllers[0];
						
						switch (key) {
							case GLFW_KEY_ENTER:       controller.input.Start    = isPressed; break;
							case GLFW_KEY_BACKSPACE:   controller.input.Select   = isPressed; break;
							case GLFW_KEY_UP:          controller.input.Up       = isPressed; break;
							case GLFW_KEY_RIGHT:       controller.input.Right    = isPressed; break;
							case GLFW_KEY_DOWN:        controller.input.Down     = isPressed; break;
							case GLFW_KEY_LEFT:        controller.input.Left     = isPressed; break;
							case GLFW_KEY_Z:           controller.input.Cross    = isPressed; break;
							case GLFW_KEY_X:           controller.input.Circle   = isPressed; break;
							case GLFW_KEY_A:           controller.input.Square   = isPressed; break;
							case GLFW_KEY_S:           controller.input.Triangle = isPressed; break;
							case GLFW_KEY_Q:           controller.input.L1       = isPressed; break;
							case GLFW_KEY_E:           controller.input.R1       = isPressed; break;
							case GLFW_KEY_1:           controller.input.L2       = isPressed; break;
							case GLFW_KEY_2:           controller.input.R2       = isPressed; break;
							case GLFW_KEY_LEFT_SHIFT:  controller.input.L3       = isPressed; break;
							case GLFW_KEY_RIGHT_SHIFT: controller.input.R3       = isPressed; break;
							default: break;
						}
					}
				}
				
			private:
				struct Channel {
					Channel() {
						stat.TX_FIFO_NOT_FULL = true;
						stat.TX_IDLE = true;
					}

					STAT stat = STAT(0);
					uint16_t mode = 0;
					uint16_t baudReload = 0;
					uint32_t baudTimer = 0;
					bool txEnabled = false;
					bool dtrOutput = false;
					bool rxEnabled = false;
					bool sio1TxOutputLevel = false;
					bool sio1RtsOutputLevel = false;
					bool sio1Unknown = false;
					uint8_t rxInterruptMode = 0;
					bool txInterruptEnabled = false;
					bool rxInterruptEnabled = false;
					bool dsrInterruptEnabled = false;
					bool sio0Selected = false;
					bool txHoldingFull = false;
					bool txActive = false;
					bool txArmed = false;
					uint8_t txHolding = 0;
					uint8_t txShift = 0;
					uint64_t txCycles = 0;
					std::array<uint8_t, 8> rxFifo = {};
					uint8_t rxHead = 0;
					uint8_t rxCount = 0;
					uint8_t lastRx = 0;
					uint8_t emptyReadsRemaining = 0;
					uint32_t dsrTimer = 0;
					bool dsrIrqPending = false;
				};

				uint32_t reloadValue(uint32_t port) const;
				uint32_t transferBitCycles(uint32_t port) const;
				uint16_t ctrlValue(uint32_t port) const;
				uint32_t readRx(uint32_t port, uint32_t size);
				void writeTx(uint32_t port, uint8_t value);
				void stepChannel(uint32_t port, uint32_t cycles);
				void stepBaud(uint32_t port, uint32_t cycles);
				void startTransfer(uint32_t port);
				void completeTransfer(uint32_t port);
				void pushRx(uint32_t port, uint8_t value);
				void evaluateIrq(uint32_t port);
				void resetChannel(uint32_t port);

				std::array<Channel, 2> channels = {};
				ConnectedDevice _connectedDevice = None;
				
			private:
				// TODO; Temp
				static DigitalController _controllers[2];
				
				::MemoryCard _memoryCard = {};
		};
	}
}
