#include "SIO.h"

#include <algorithm>
#include <cassert>
#include <limits>

#include "../IRQ.h"
#include "../../Utils/Bitwise.h"

DigitalController Emulator::IO::SIO::_controllers[] = {};

void Emulator::IO::SIO::step(uint32_t cycles) {
	for(uint32_t port = 0; port < channels.size(); port++) {
		stepBaud(port, cycles);
		stepChannel(port, cycles);
		evaluateIrq(port);
	}
}

uint32_t Emulator::IO::SIO::load(uint32_t addr, uint32_t size) {
	const uint32_t port = (addr - 0x1F801040) / 0x10;
	const uint32_t reg = addr - port * 0x10;
	auto& channel = channels[port];

	if(reg == 0x1F801040) {
		return readRx(port, size);
	}

	if(reg == 0x1F801044) {
		channel.stat.BAUDRATE_TIMER = channel.baudTimer & 0x1FFFFF;
		return channel.stat.reg;
	}

	if(reg == 0x1F801048) {
		return channel.mode;
	}

	if(reg == 0x1F80104A) {
		return ctrlValue(port);
	}

	if(reg == 0x1F80104E) {
		return channel.baudReload;
	}

	assert(false);
	return 0xFF;
}

void Emulator::IO::SIO::store(uint32_t addr, uint32_t val, uint32_t size) {
	const uint32_t port = (addr - 0x1F801040) / 0x10;
	const uint32_t reg = addr - port * 0x10;
	auto& channel = channels[port];

	if(reg == 0x1F801040) {
		writeTx(port, static_cast<uint8_t>(val));
		return;
	}

	if(reg == 0x1F801048) {
		channel.mode = static_cast<uint16_t>(val);
		return;
	}

	if(reg == 0x1F80104A) {
		setCtrl(port, val);
		return;
	}

	if(reg == 0x1F80104E) {
		channel.baudReload = static_cast<uint16_t>(val);
		channel.baudTimer = reloadValue(port);
		return;
	}

	assert(false);
}

uint32_t Emulator::IO::SIO::cyclesUntilNextEvent() const {
	uint32_t next = std::numeric_limits<uint32_t>::max();

	for(const auto& channel : channels) {
		if(channel.txActive)
			next = std::min<uint32_t>(next, static_cast<uint32_t>(channel.txCycles));

		if(channel.dsrTimer > 0)
			next = std::min(next, channel.dsrTimer);
	}

	return next;
}

void Emulator::IO::SIO::setCtrl(uint32_t port, uint32_t val) {
	auto& channel = channels[port];
	const uint16_t ctrl = static_cast<uint16_t>(val);

	channel.txEnabled = Utils::Bitwise::getBit(ctrl, 0);
	channel.dtrOutput = Utils::Bitwise::getBit(ctrl, 1);
	channel.rxEnabled = Utils::Bitwise::getBit(ctrl, 2);
	channel.sio1TxOutputLevel = Utils::Bitwise::getBit(ctrl, 3);
	channel.sio1RtsOutputLevel = Utils::Bitwise::getBit(ctrl, 5);
	channel.sio1Unknown = Utils::Bitwise::getBit(ctrl, 7);
	channel.rxInterruptMode = (ctrl >> 8) & 0x3;
	channel.txInterruptEnabled = Utils::Bitwise::getBit(ctrl, 10);
	channel.rxInterruptEnabled = Utils::Bitwise::getBit(ctrl, 11);
	channel.dsrInterruptEnabled = Utils::Bitwise::getBit(ctrl, 12);
	channel.sio0Selected = Utils::Bitwise::getBit(ctrl, 13);

	if(Utils::Bitwise::getBit(ctrl, 4)) {
		channel.stat.RX_PARITY_ERROR = false;
		channel.stat.SIO1_RX_FIFO_OVERRUN = false;
		channel.stat.SIO1_RX_BAD_STOP_BIT = false;
		channel.stat.INTERRUPT_REQUEST = false;
	}

	if(Utils::Bitwise::getBit(ctrl, 6)) {
		resetChannel(port);
		return;
	}

	if(port == 1 && !channel.rxEnabled) {
		channel.rxHead = 0;
		channel.rxCount = 0;
		channel.stat.RX_FIFO_NOT_EMPTY = false;
	}

	if(port == 0 && !channel.dtrOutput) {
		_connectedDevice = None;
		_memoryCard.reset();
		_controllers[0].reset();
		_controllers[1].reset();
		channel.stat.DSR_INPUT_LEVEL = false;
	}
}

uint32_t Emulator::IO::SIO::reloadValue(uint32_t port) const {
	const auto& channel = channels[port];
	uint32_t factor = 1;

	if(port == 0) {
		switch(channel.mode & 0x3) {
			case 2: factor = 16; break;
			case 3: factor = 64; break;
			default: break;
		}
	}

	return std::min<uint32_t>((static_cast<uint32_t>(channel.baudReload) * factor) / 2, 0x1FFFFF);
}

uint32_t Emulator::IO::SIO::transferBitCycles(uint32_t port) const {
	const auto& channel = channels[port];
	uint32_t factor = 1;

	switch(channel.mode & 0x3) {
		case 0: factor = port == 0 ? 1 : 0; break;
		case 1: factor = 1; break;
		case 2: factor = 16; break;
		case 3: factor = 64; break;
	}

	if(factor == 0) {
		return 0;
	}

	const uint32_t reloadFactor = static_cast<uint32_t>(channel.baudReload) * factor;
	const uint32_t minimum = port == 0 ? 1 : factor;
	return std::max(reloadFactor & ~1u, minimum);
}

uint16_t Emulator::IO::SIO::ctrlValue(uint32_t port) const {
	const auto& channel = channels[port];
	uint16_t ctrl = 0;

	ctrl |= channel.txEnabled << 0;
	ctrl |= channel.dtrOutput << 1;
	ctrl |= channel.rxEnabled << 2;
	ctrl |= channel.sio1TxOutputLevel << 3;
	ctrl |= channel.sio1RtsOutputLevel << 5;
	ctrl |= (channel.sio1Unknown && (channel.mode & 0x3)) << 7;
	ctrl |= (channel.rxInterruptMode & 0x3) << 8;
	ctrl |= channel.txInterruptEnabled << 10;
	ctrl |= channel.rxInterruptEnabled << 11;
	ctrl |= channel.dsrInterruptEnabled << 12;
	ctrl |= channel.sio0Selected << 13;
	return ctrl;
}

uint32_t Emulator::IO::SIO::readRx(uint32_t port, uint32_t size) {
	auto& channel = channels[port];
	const uint32_t bytes = std::min(size, 4u);
	uint32_t value = 0;

	for(uint32_t i = 0; i < bytes; i++) {
		uint8_t byte = 0;

		if(channel.rxCount > 0) {
			byte = channel.rxFifo[(channel.rxHead + i) & 7];
		} else if(channel.emptyReadsRemaining > i) {
			byte = channel.lastRx;
		}

		value |= static_cast<uint32_t>(byte) << (i * 8);
	}

	const uint32_t removed = size == 4 ? 4 : 1;

	if(channel.rxCount > 0) {
		const uint32_t consumed = std::min<uint32_t>(removed, channel.rxCount);
		channel.rxHead = (channel.rxHead + removed) & 7;
		channel.rxCount -= consumed;

		if(channel.rxCount == 0) {
			channel.emptyReadsRemaining = 2;
		}
	} else {
		channel.emptyReadsRemaining = removed >= channel.emptyReadsRemaining
			? 0
			: channel.emptyReadsRemaining - removed;
	}

	channel.stat.RX_FIFO_NOT_EMPTY = channel.rxCount != 0;
	return value;
}

void Emulator::IO::SIO::writeTx(uint32_t port, uint8_t value) {
	auto& channel = channels[port];
	channel.txHolding = value;
	channel.txHoldingFull = true;
	channel.txArmed = channel.txArmed || channel.txEnabled;
	channel.stat.TX_FIFO_NOT_FULL = false;
	channel.stat.TX_IDLE = false;
}

void Emulator::IO::SIO::stepChannel(uint32_t port, uint32_t cycles) {
	auto& channel = channels[port];

	if(channel.dsrTimer > 0) {
		if(cycles >= channel.dsrTimer) {
			channel.dsrTimer = 0;
			channel.stat.DSR_INPUT_LEVEL = false;
			channel.dsrIrqPending = true;
		} else {
			channel.dsrTimer -= cycles;
		}
	}

	uint64_t remaining = cycles;

	while(remaining > 0) {
		if(!channel.txActive) {
			if(!channel.txHoldingFull || (!channel.txEnabled && !channel.txArmed)) {
				break;
			}

			if(port == 0 && !channel.dtrOutput) {
				break;
			}

			startTransfer(port);

			if(!channel.txActive) {
				break;
			}
		}

		if(remaining < channel.txCycles) {
			channel.txCycles -= remaining;
			break;
		}

		remaining -= channel.txCycles;
		completeTransfer(port);
	}
}

void Emulator::IO::SIO::stepBaud(uint32_t port, uint32_t cycles) {
	auto& channel = channels[port];
	const uint32_t reload = reloadValue(port);

	if(reload == 0) {
		channel.baudTimer = 0;
		return;
	}

	if(channel.baudTimer == 0) {
		channel.baudTimer = reload;
	}

	if(cycles < channel.baudTimer) {
		channel.baudTimer -= cycles;
		return;
	}

	const uint64_t remaining = static_cast<uint64_t>(cycles) - channel.baudTimer;
	const uint32_t remainder = remaining % reload;
	channel.baudTimer = remainder == 0 ? reload : reload - remainder;
}

void Emulator::IO::SIO::startTransfer(uint32_t port) {
	auto& channel = channels[port];
	const uint32_t bitCycles = transferBitCycles(port);

	if(bitCycles == 0) {
		return;
	}

	channel.txShift = channel.txHolding;
	channel.txHoldingFull = false;
	channel.txActive = true;
	channel.txArmed = false;
	channel.txCycles = static_cast<uint64_t>(bitCycles) * 8;
	channel.stat.TX_FIFO_NOT_FULL = true;
}

void Emulator::IO::SIO::completeTransfer(uint32_t port) {
	auto& channel = channels[port];

	if(port == 0) {
		uint8_t rxData = 0xFF;

		if(_connectedDevice == None) {
			if(channel.txShift == 0x01) {
				_connectedDevice = Controller;
			} else if(channel.txShift == 0x81) {
				_connectedDevice = MemoryCard;
			}
		}

		if(_connectedDevice == Controller) {
			rxData = static_cast<uint8_t>(_controllers[channel.sio0Selected].load(channel.txShift));
			channel.stat.DSR_INPUT_LEVEL = _controllers[channel.sio0Selected].interrupt;

			if(channel.stat.DSR_INPUT_LEVEL) {
				channel.dsrTimer = 500;
			}
		} else if(_connectedDevice == MemoryCard) {
			rxData = static_cast<uint8_t>(_memoryCard.handle(channel.txShift));
			channel.stat.DSR_INPUT_LEVEL = _memoryCard._interrupt;

			if(channel.stat.DSR_INPUT_LEVEL) {
				channel.dsrTimer = 1800;
			}
		} else {
			channel.stat.DSR_INPUT_LEVEL = false;
		}

		pushRx(port, rxData);

		if(!channel.stat.DSR_INPUT_LEVEL) {
			_connectedDevice = None;
		}
	}

	channel.txActive = false;
	channel.txCycles = 0;

	if(channel.txHoldingFull
		&& (channel.txEnabled || channel.txArmed)
		&& (port != 0 || channel.dtrOutput)) {
		startTransfer(port);
	} else if(!channel.txHoldingFull) {
		channel.stat.TX_IDLE = true;
	}
}

void Emulator::IO::SIO::pushRx(uint32_t port, uint8_t value) {
	auto& channel = channels[port];

	if(channel.rxCount == channel.rxFifo.size()) {
		channel.rxFifo[(channel.rxHead + channel.rxFifo.size() - 1) & 7] = value;

		if(port == 1) {
			channel.stat.SIO1_RX_FIFO_OVERRUN = true;
		}
	} else {
		channel.rxFifo[(channel.rxHead + channel.rxCount) & 7] = value;
		channel.rxCount++;

		for(uint32_t i = channel.rxCount; i < channel.rxFifo.size(); i++) {
			channel.rxFifo[(channel.rxHead + i) & 7] = value;
		}
	}

	channel.lastRx = value;
	channel.emptyReadsRemaining = 2;
	channel.stat.RX_FIFO_NOT_EMPTY = true;
}

void Emulator::IO::SIO::evaluateIrq(uint32_t port) {
	auto& channel = channels[port];
	const uint32_t rxThreshold = 1u << channel.rxInterruptMode;
	const bool txIrq = channel.txInterruptEnabled
		&& (channel.stat.TX_FIFO_NOT_FULL || channel.stat.TX_IDLE);
	const bool rxIrq = channel.rxInterruptEnabled && channel.rxCount >= rxThreshold;
	const bool dsrIrq = channel.dsrInterruptEnabled && channel.dsrIrqPending;

	if((txIrq || rxIrq || dsrIrq) && !channel.stat.INTERRUPT_REQUEST) {
		channel.stat.INTERRUPT_REQUEST = true;
		channel.dsrIrqPending = false;
		IRQ::trigger(port == 0 ? IRQ::PadMemCard : IRQ::SIO);
	}
}

void Emulator::IO::SIO::resetChannel(uint32_t port) {
	auto& channel = channels[port];
	channel = Channel{};

	if(port == 0) {
		_connectedDevice = None;
		_memoryCard.reset();
		_controllers[0].reset();
		_controllers[1].reset();
	}
}
