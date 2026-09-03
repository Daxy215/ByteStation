#include "interconnect.h"

#include <algorithm>
#include <string>

#include "Memories/Ram.h"

#include "../CPU/CPU.h"

bool Interconnect::step(uint32_t cycles) {
    if (!_hardwareTickScheduled) {
        _hardwareTickScheduled = true;
        _lastHardwareTickCycle = scheduler.currentCycle();
        scheduler.scheduleEvent([this] { hardwareTick(); }, 1);
    }

    if (!_spuTickScheduled) {
        _spuTickScheduled = true;
        _lastSpuTickCycle = scheduler.currentCycle();
        scheduler.scheduleEvent([this] { spuTick(); }, SPU_TICK_CYCLES);
    }

    _vblankPending = false;

    scheduler.addCycles(cycles);

    _dma.step();

    _sioPendingCycles += cycles;

    if (_sioPendingCycles >= _sio.cyclesUntilNextEvent()) {
        _sio.step(_sioPendingCycles);
        _sioPendingCycles = 0;
    }

    return _vblankPending;
}

void Interconnect::hardwareTick() {
    uint64_t now = scheduler.currentCycle();
    uint32_t elapsed = static_cast<uint32_t>(now - _lastHardwareTickCycle);

    while (elapsed > 0) {
        uint32_t chunk = std::min(elapsed, _gpu->cpuCyclesUntilNextCRTCEvent());

        _cdrom.step(chunk);

        if (_gpu->step(chunk)) {
            IRQ::trigger(IRQ::VBlank);
            _vblankPending = true;
        }

        _timers.step(chunk, _gpu->lastDotTicks);
        _timers.sync(_gpu->isInHBlank, _gpu->isInVBlank, _gpu->dotClockDivider());

        elapsed -= chunk;
    }

    _lastHardwareTickCycle = now;

    uint32_t next = std::min({_cdrom.cyclesUntilNextInterrupt(), _gpu->cpuCyclesUntilNextCRTCEvent(), _timers.cyclesUntilNextEvent()});
    next = std::max(next, 1u);

    scheduler.scheduleEvent([this] { hardwareTick(); }, next);
}

void Interconnect::spuTick() {
    uint64_t now = scheduler.currentCycle();
    uint32_t elapsed = static_cast<uint32_t>(now - _lastSpuTickCycle);

    spu.step(elapsed);

    _lastSpuTickCycle = now;

    scheduler.scheduleEvent([this] { spuTick(); }, SPU_TICK_CYCLES);
}

uint32_t Interconnect::dmaReg(uint32_t offset) {
    uint32_t align = offset & 3;
    offset = offset & ~3;
    
    uint32_t major = (offset & 0x70) >> 4;
    uint32_t minor = offset & 0xF;
    
    switch (major) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: {
            // Per-channel registers 0...6
            Channel& channel = _dma.getChannel(PortC::fromIndex(major));
            
            switch (minor) {
                case 0:
                    return channel.base >> (align * 8); // MADR
                case 4:
                    return channel.blockControl() >> (align * 8);
                case 8:
                    return channel.control() >> (align * 8);
                default:
                    throw std::runtime_error("Unhandled DMA minor read at " + std::to_string(minor));
                    break;
            }
            
            break;
        }
        // Common DMA registers
        case 7: {
            switch (minor) {
                case 0:
                    return _dma.control >> (align * 8);
                case 4:
                    return _dma.interrupt() >> (align * 8);
                default:
                    throw std::runtime_error("Unhandled DMA read at " + std::to_string(offset));
            }
        }
    default:
        throw std::runtime_error("Unhandled DMA read at " + std::to_string(offset));
    }
}

void Interconnect::doDma(Port port) {
    // DMA transfer has been started, for now let's
    // process everything in one pass (i.e. no
    // chopping or priority handling)
    if (!_dma.channelEnabled(port)) {
        return;
    }
    
    Channel& channel = _dma.getChannel(port);

    if (channel.sync == LinkedList) {
        dmaLinkedList(port);
    } else {
        dmaBlock(port);
    }

    channel.done(_dma, port);
}

void Interconnect::dmaBlock(Port port) {
    Channel& channel = _dma.getChannel(port);

    int32_t increment = (channel.step == Increment) ? 4 : -4;
    uint32_t addr = channel.base/* & 0x00FFFFFF*/;
    
    // Transfer size in words
    std::optional<uint32_t> transferSize = channel.transferSize();

    if (!transferSize.has_value()) {
        return;
    }

    uint32_t remsz = transferSize.value();

    while (remsz > 0) {
        // Not sure what happens if the address is bogus,
        // Mednafen just makes addr this way, maybe that's,
        // how the hardware behaves (i.e. the RAM address,
        // wraps and the two LSB are ignored, seems reasonable enough.
        uint32_t curAddr = addr & 0x1FFFFC;
        
        switch (channel.direction) {
            case FromRam: {
                auto srcWord = _ram.load<uint32_t>(curAddr);
                
                switch (port) {
                    case Gpu:
                        _gpu->gp0(srcWord);
                        
                        break;
                    case MdecIn:
                        if (!mdec.dataInRequest())
                            break;
                        
                        mdec.store(0, srcWord);
                        
                        break;
                    case Spu:
                        spu.store(0x1F801DA8, (srcWord) & 0xFFFF);
                        spu.store(0x1F801DA8, (srcWord >> 16) & 0xFFFF);

                        break;
                    default:
                        throw std::runtime_error("Unhandled DMA destination port " + std::to_string(static_cast<uint8_t>(port)));
                        break;
                }
                
                break;
            }
            case ToRam: {
                uint32_t srcWord = 0;

                switch(port) {
                    case Otc:
                        // Clear ordering table
                        if(remsz == 1) {
                            // Last entry contains the end of the table marker
                            srcWord = 0xFFFFFF;
                        } else {
                            // Pointer to the previous entry
                            srcWord = (addr - 4) & 0x1FFFFF;
                        }
                        
                        break;
                    case Port::Gpu:
                        // This gets called before the,
                        // menu pops up after the PS logo
                        
                        /**
                         * This for the menu. It tries to read from VRAM,
                         * at 640, which seems to be from a quad that it,
                         * draws using 0x38, but I'm drawing it using OpenGL,
                         * so I'm unsure how to really do this...
                         */
                        srcWord = _gpu->read();
                        
                        break;
                    case Port::CdRom: {
                        srcWord = 0;
                        
                        srcWord |= _cdrom.readByte() << 0;
                        srcWord |= _cdrom.readByte() << 8;
                        srcWord |= _cdrom.readByte() << 16;
                        srcWord |= _cdrom.readByte() << 24;
                        
                        break;
                    }
                    
                    case Port::MdecOut: {
                        if (!mdec.dataOutRequest())
                            break;
                        
                        srcWord = mdec.load(0);
                        //srcWord = 0x7C007C00;

                        break;
                    }
                    case Spu: {
                        srcWord = 0;

                        // TODO: Confirm
                        srcWord |= spu.load(0x1F801DA8) << 0;
                        srcWord |= spu.load(0x1F801DA8) << 16;

                        break;
                    }
                    default:
                        throw std::runtime_error("Unhandled DMA source port" + std::to_string(static_cast<uint8_t>(port)));    
                        break;
                }
                
                _ram.store<uint32_t>(curAddr, srcWord);
                
                break;
            }
        }
        
        addr += increment;
        remsz -= 1;
    }

    channel.setBase(addr);

    if (channel.sync != LinkedList) {
        channel.blockCount = 0;
    }
}

void Interconnect::dmaLinkedList(Port port) {
    Channel& channel = _dma.getChannel(port);
    
    uint32_t addr = channel.base;
    
    if(channel.direction == ToRam) {
        throw std::runtime_error("Invalid DMA direction for linked list mode");
    }
    
    // I don't know if the DMA even supports,
    // linked list mode for anything besides the GPU
    if(port != Gpu) {
        throw std::runtime_error("Attempted linked list DMA on port " + std::to_string(static_cast<uint8_t>(port)));
    }

    while (true) {
        uint32_t header = _ram.load<uint32_t>(addr);

        uint32_t count = header >> 24;
        uint32_t next  = header & 0x00FFFFFF;

        uint32_t packetAddr = (addr + 4) & 0x00FFFFFF;

        while (count > 0) {
            uint32_t command = _ram.load<uint32_t>(packetAddr);
            _gpu->gp0(command);

            packetAddr = (packetAddr + 4) & 0x00FFFFFF;
            --count;
        }

        channel.setBase(next);

        // The end-of-table makrer is usually 0xFFFFFF but,
        // mednafen only checks for the MSB so maybe that's what
        // the harder does? Since this bit is not part of any,
        // valid address it makes some sense. I'll have to test
        // that at some point..
        if (next & 0x00800000) {
            if (next != 0x00FFFFFF) {
                _dma.forceIrq = true;
            }

            break;
        }

        addr = next;
    }

    /*while (true) {
        uint32_t headerAddr = addr & 0x1FFFFC;
        uint32_t header = _ram.load<uint32_t>(headerAddr);

        uint32_t count = header >> 24;
        uint32_t next = header & 0x00FFFFFF;

        uint32_t packetAddr = (addr + 4) & 0x00FFFFFF;

        while (count > 0) {
            uint32_t command = _ram.load<uint32_t>(packetAddr & 0x1FFFFC);
            _gpu->gp0(command);

            packetAddr = (packetAddr + 4) & 0x00FFFFFF;
            count--;
        }

        channel.base = next;

        if (next & 0x00800000) {
            if (next != 0x00ffffff) {
                _dma.forceIrq = true;
            }

            break;
        }

        addr = next;
    }*/

    // TODO; idk if this is correct but,
    // TODO; Tekken 3 is stuck in a loop here..
    // TODO; so... imma just
    //std::unordered_set<uint32_t> addrs;
    /*while(true) {
        // In linked list mode, each entry,
        // starts with a "header" word.
        // The high byte contains the number
        // of words in the "packet"
        // (not counting the header word)
        auto header = _ram.load<uint32_t>(addr);
        uint32_t remsz = header >> 24;
        
        while(remsz > 0) {
            addr = (addr + 4) & 0x1FFFFC;
            
            const auto command = _ram.load<uint32_t>(addr);
            
            _gpu->gp0(command);
            
            remsz -= 1;
        }
        
        // The end-of-table makrer is usually 0xFFFFFF but,
        // mednafen only checks for the MSB so maybe that's what
        // the harder does? Since this bit is not part of any,
        // valid address it makes some sense. I'll have to test
        // that at some point..
        if((header & 0x800000) != 0) {
            break;
        }
        
        // TODO; Debug y CPU stuck in a loop,
        // TODO; Games that has this issue so far is;
        // TODO; Tekken 3, 
        if (addrs.find(addr) != addrs.end()) {
            printf("beeeeeeeeeh\n");
            break;
        }
        
        addrs.insert(addr);
        
        addr = header & 0x1FFFFC;
    }*/
}

void Interconnect::setDmaReg(uint32_t offset, uint32_t val) {
    uint32_t align = offset & 3;
    val = val << (align * 8);
    offset = offset & ~3;
    
    uint32_t major = (offset & 0x70) >> 4;
    uint32_t minor = offset & 0xF;
    
    std::optional<Port> activePort = std::nullopt;
    
    switch (major) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: {
                Port port = PortC::fromIndex(major);
                Channel& channel = _dma.getChannel(port);

                switch (minor) {
                    case 0:
                        channel.setBase(val); // MADR
                        break;
                    case 4:
                        channel.setBlockControl(val);
                        break;
                    case 8:
                        channel.setControl(val);
                        break;
                    default:
                        throw std::runtime_error("Unhandled DMA write " + std::to_string(offset) + " : " + std::to_string(val));
                }

                if(channel.active()) {
                    activePort = port;
                }

                break;
        }
        case 7: {
            switch (minor) {
                case 0:
                    _dma.setControl(val); // DPCR (0x1F8010F0)
                    break;
                case 4:
                    _dma.setInterrupt(val); // DICR (0x1F8010F4)
                    break;
                default:
                    throw std::runtime_error("Unhandled DMA write " + std::to_string(offset) + " : " + std::to_string(val));
            }
            break;
        }
        default:
            throw std::runtime_error("Unhandled DMA write " + std::to_string(offset) + " : " + std::to_string(val));
    }

    if(activePort.has_value()) {
        doDma(activePort.value());
    }
}
