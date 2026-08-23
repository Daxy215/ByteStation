#pragma once

#include <iostream>
#include <stdint.h>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

#include "../DMA/Dma.h"
#include "../GPU/Gpu.h"
#include "../Memory/CDROM/CDROM.h"
#include "../Memory/IO/SIO.h"
#include "../Memory/IRQ.h"
#include "../Memory/Memories/ScratchPad.h"
#include "../Memory/Timers/Timers.h"
#include "../SPU/Stolen/spu.h"
#include "Bios/Bios.h"
#include "MDEC/MDEC.h"
#include "Memories/Ram.h"
#include "Range.h"
#include "Scheduler.h"
#include "SPU/SPU.h"

//#include "CDROM/cdrom.h"
//#include "../SPU/SPU.h"

/**
 * This class is used to allow the BIOS to communicate with the CPU! :D
 */
/*class Ram;
class Dma;*/
class Bios;

struct CacheControl {
    uint32_t val = 0;

    CacheControl(uint32_t val) : val(val) {}

    bool isCacheEnabled() {
        return (val & 0x800) != 0;
    }

    bool isInTagMode() {
        return (val & 0x4) != 0;
    }
};

struct ICache {
    uint32_t tag;
    uint32_t data[4];
    uint8_t valid;

    ICache() : tag(0), data{0, 0, 0, 0}, valid(0) {}
};

class Interconnect {
public:
    Interconnect()  : memControl{}, _gpu(nullptr), _spu(nullptr), scheduler(this) {
        
    }
    
    Interconnect(Emulator::Gpu* gpu/*, Emulator::SPU spu*/)
        : memControl{}, _gpu(gpu), _spu(new spu::SPU()), scheduler(this)
    /*, spu(spu)*/ {
        _ram = Ram();

        // TODO;
        _bios = Bios("../../BIOS/ps-22a.bin");
        //_bios = Bios("../../BIOS/openbios.bin");
        //_bios = Bios("../BIOS/openbios2.bin");
        //_bios = Bios("../BIOS/openbios-fastboot.bin");
        //_bios = Bios("../BIOS/openbios-unirom.bin");
        
        _dma = Dma();
        
        if (false) {
            mdec.reset();
            mdec.luminanceQuantTable.fill(1);
            mdec.colorQuantTable.fill(1);
            mdec.scaleTable.fill(1);
            
            // Using 10-bit DC values only, AC=0, simple test
            // Structure: DCT(DC=Y, Q=1), RLE(AC=0, LEN=0)
            std::vector<uint16_t> testInput;
            
            mdec.command.DataOutputSigned = 1;
            // For colored macroblock (DataOutputDepth=3)
            mdec.command.DataOutputDepth = 3;
            mdec.command.Op = 1; // Decode macroblock
            mdec.command.NumberOfParameterWords = 10;
            
            auto makeDCT = [](uint16_t dc) -> uint16_t {
                return (1 << 10) | (dc & 0x3FF); // Q=1, DC in lower 10 bits
            };
            
            for (int i = 0; i < 64; i++) testInput.push_back(makeDCT(512)); // Cr
            for (int i = 0; i < 64; i++) testInput.push_back(makeDCT(512)); // Cb
            for (int i = 0; i < 64*4; i++) testInput.push_back(makeDCT(512)); // Yblk0..3
            
            mdec.input = testInput;
            mdec.command.NumberOfParameterWords = testInput.size();
            
            mdec.decodeBlocks();
            
            std::cout << "Decoded RGB555 pixels (first 16):\n";
            for (int i = 0; i < 16; i++) {
                std::cout << std::hex << mdec.output[i] << " ";
            }
            std::cout << std::endl;
        }
    }
    
    bool step(uint32_t cycles);
    void hardwareTick();

    inline uint32_t loadInstruction(uint32_t addr) {
        lastICacheMiss = false;

        if(_cacheControl.isCacheEnabled() == 0 || addr >= 0xA0000000) {
            lastICacheMiss = true;

            uint32_t abs_addr = map::maskRegion(addr);
            uint32_t offset = 0;
            
            if (map::RAM.contains(abs_addr, offset)) {
                return _ram.load<uint32_t>(offset);
            }
            
            if (map::BIOS.contains(abs_addr, offset)) {
                return _bios.load<uint32_t>(offset);
            }
            
            // TODO; Testing
            return load<uint32_t>(addr);
        }
        
        // I-Cache
        
        // Cache is only active in the cached regions (KUSEG and KSEG0).
        // Tag = physical_address[31:12] | valid[3:0]
        uint32_t physical = map::maskRegion(addr);
        uint32_t tag   = physical >> 12;         // addr[31:12]
        uint16_t index = (physical >> 4) & 0xFF; // addr[11:04]
        uint8_t word   = (physical >> 2) & 3;    // addr[03:02] -> which word in line

        ICache& line = icache[index];

        if (line.tag == tag && (line.valid & (1 << word))) {
            return line.data[word];
        }

        lastICacheMiss = true;

        if (line.tag != tag) {
            line.tag = tag;
            line.valid = 0;
        }

        uint32_t lineBase = addr & ~0xFu;

        for (uint8_t i = word; i < 4; i++) {
            line.data[i] = load<uint32_t>(lineBase + i * 4);
            line.valid |= (1 << i);
        }

        return line.data[word];
    }
    
    template<typename T>
    T load(uint32_t addr) {
        if (flatMemoryMode) {
            T value = 0;

            for (size_t i = 0; i < sizeof(T); i++) {
                auto it = flatMemory.find(addr + i);
                value |= static_cast<T>(it != flatMemory.end() ? it->second : 0) << (8 * i);
            }

            return value;
        }

        uint32_t abs_addr = map::maskRegion(addr);
        uint32_t offset = 0;

        if (map::RAM.contains(abs_addr, offset)) {
            T value = _ram.load<T>(offset);

            // For psx-redux
            if constexpr (sizeof(T) == 4) {
                uint32_t biu = _cacheControl.val;

                bool dsOnly = (biu & (1u << 7)) && !(biu & (1u << 3));
                bool cached = addr < 0xa0000000;

                if (dsOnly && cached) {
                    _scratchPad.store<uint32_t>(((addr >> 2) & 0xff) * 4, static_cast<uint32_t>(value));
                }
            }

            return value;
        }
        
        if (map::SCRATCHPAD.contains(abs_addr, offset)) {
            if (addr > 0xa0000000) {
                //throw std::runtime_error("ScratchPad access through uncached memory");
            }
            
            return _scratchPad.load<T>(offset);
        }
        
        if (map::BIOS.contains(abs_addr, offset)) {
            return _bios.load<T>(offset);
        }
        
        if (map::IRQ_CONTROL.contains(abs_addr, offset)) {
            switch (offset) {
                case 0: {
                    return _irq.getStatus();
                }
                
                case 4: {
                    return _irq.getMask();
                }
                
                default: {
                    throw std::runtime_error("Error; Unsupported IRQ offset");
                }
            }
        }
        
        if (map::DMA.contains(abs_addr, offset)) {
            return dmaReg(offset);
        }
        
        if (map::PCSX_REDUX_EXPANSION.contains(abs_addr, offset)) {
            /*switch (offset) {
                case 0:
                        // Read "PCSX" expansion ID
                        return 0;
                case 1: 
                        // Handle console putchar (write character to console)
                        return 0;
                case 2: 
                        // Handle debug breakpoint (pause emulator)
                        return 0;
                case 3: 
                        // Handle exit code (set exit code)
                        return 0;
                case 4: 
                        // Handle notification message pointer (show pop-up)
                        return 0;
                default:
                    throw std::runtime_error("Unhandled PCSX-Redux Expansion access");
            }*/
            
            return 0;
        }
        
        // 0x1f801810
        if (map::GPU.contains(abs_addr, offset)) {
            switch (offset) {
                case 0:
                    return _gpu->read();
                case 4: {
                    return _gpu->status();
                }
                default:
                    throw std::runtime_error("Unhandled GPU load at address 0x");
            }
            
            printf("broo");
        }
        
        if (map::TIMERS.contains(abs_addr, offset)) {
            return _timers.load(offset);
        }
        
        if (map::CDROM.contains(abs_addr, offset)) {
            return _cdrom.load(offset);
            //return psx_cdrom_read8(_cdrom, offset);
        }
        
        if (map::MDEC.contains(abs_addr, offset)) {
            //throw std::runtime_error("Unhandled MDEC load at address 0x" + to_hex(addr));
            return mdec.load(offset);
        }
        
        if (map::SPU.contains(abs_addr, offset)) {
            // https://github.com/psx-spx/psx-spx.github.io/blob/master/docs/soundprocessingunitspu.md
            //std::cerr << "SPU load register; " << to_hex(addr) << "\n";
            //static int h = 0;
            uint16_t v = _spu->read(offset) | _spu->read(offset + 1) << 8;

            /*if constexpr (sizeof(T) == 1) {
                uint32_t regAddr = abs_addr & ~1u;
                uint16_t reg = static_cast<uint16_t>(spu.load(regAddr));
                return static_cast<T>((reg >> ((abs_addr & 1u) * 8)) & 0xFF);
            } else if constexpr (sizeof(T) == 2) {
                return static_cast<T>(spu.load(abs_addr & ~1u));
            } else if constexpr (sizeof(T) == 4) {
                uint32_t lo = spu.load(abs_addr & ~1u);
                uint32_t hi = spu.load((abs_addr & ~1u) + 2);
                return static_cast<T>(lo | (hi << 16));
            }*/
            
            //auto f = spu.load(addr);
            //h++;

            //if (f != v) {
           //     v = _spu->read(offset) | _spu->read(offset + 1) << 8;
            //    f = spu.load(addr);
                //printf("AYO %x != %x\n", v, f);
            //}

            //return f;
            return v;

            //return spu.load(addr);
        }

        if (auto _ = map::PADMEMCARD.contains(abs_addr, offset)) {
            return _sio.load(abs_addr, sizeof(T));
        }

        if (auto _ = map::RAM_SIZE.contains(abs_addr, offset)) {
            return _ramSize;
        }

        if (map::MEMCONTROL.contains(abs_addr, offset)) {
            if (sizeof(T) != 4) {
                throw std::runtime_error("Unhandled MEM_CONTROL access (" + std::to_string(sizeof(T)) + ")");
            }

            uint32_t index = (offset >> 2);
            return memControl[index];
        }

        if (auto _ = map::CACHECONTROL.contains(abs_addr, offset)) {
            if (sizeof(T) != 4) {
                throw std::runtime_error("Unhandled cache control access (" + std::to_string(sizeof(T)) + ")");
            }

            return _cacheControl.val;
        }

        if (auto _ = map::EXPANSION1.contains(abs_addr, offset)) {
            T r = 0;

            /*for(size_t i = 0; i < sizeof(T); i++) {
                // Simulate that nothing is connected
                uint8_t data = 0;

                r |= (static_cast<uint32_t>(data) << (8 * i));
            }*/

            return r;
        }

        if (map::EXPANSION2.contains(abs_addr, offset)) {
            // https://psx-spx.consoledev.net/expansionportpio/#1f802021hread-sra-duart-status-register-a-r
            if (offset == 0) {
                /**
                 * 0    Unknown, used for something
                 * 1    Unknown/unused
                 * 2    Unknown, used for something
                 * 3    TTY/Atcons TX Ready     (0=Busy, 1=Ready)
                 * 4    TTY/Atcons RX Available (0=None, 1=Yes)
                 * 5-7  Unknown/unused
                 */
                return 0x04 | 0x08;
            }

            if(offset == 0x21) {
                // UART status register A.
                // Just indicating that,
                // the bit for "Tx ready" is set.
                //return 1 << 2; // 4
                return 0x04 | 0x08;
            }

            throw std::runtime_error("Unhandled EXPANSION_2 load at address 0x");
        }

        // TODO: Hardcoded due to a test
        if (addr == 0x1FA00000) {
            // Expansion Region 3 (default 1 byte, max 2 MBytes)

            /**
             * 1FA00000h - Not used by BIOS or any PSX games
             * 1FA00000h - POST3 (similar to POST, but PS2 BIOS uses this address)
             */

            return 0;
        }

        //throw std::runtime_error("Unhandled load at address 0x");
        return 0;
    }

    template<typename T>
    void store(uint32_t addr, T val) {
        if (flatMemoryMode) {
            for (size_t i = 0; i < sizeof(T); i++)
                flatMemory[addr + i] = static_cast<uint8_t>(val >> (8 * i));

            return;
        }

        uint32_t abs_addr = map::maskRegion(addr);
        uint32_t offset = 0;

        if (map::RAM.contains(abs_addr, offset)) {
            _ram.store<T>(offset, val);

            return;
        }

        if (map::SCRATCHPAD.contains(abs_addr, offset)) {
            if (addr > 0xa0000000) {
                //throw std::runtime_error("ScratchPad access through uncached memory");
            }

            _scratchPad.store<T>(offset, val);

            return;
        }

        if (map::IRQ_CONTROL.contains(abs_addr, offset)) {
            switch (offset) {
                case 0: {
                    _irq.acknowledge(static_cast<uint16_t>(val));
                    break;
                }

                case 4: {
                    _irq.setMask(static_cast<uint16_t>(val));
                    break;
                }

                default: {
                    throw std::runtime_error("Unhandled IRQ control: 0x");
                }
            }

            return;
        }

        if (map::DMA.contains(abs_addr, offset)) {
            setDmaReg(offset, val);

            return;
        }

        if (map::GPU.contains(abs_addr, offset)) {
            switch (offset) {
                case 0:
                    _gpu->gp0(val);
                    break;
                case 4:
                    _gpu->gp1(val);
                    break;
                default:
                    throw std::runtime_error("GPU write " + std::to_string(offset) + ": 0x");
            }

            return;
        }

        if (map::TIMERS.contains(abs_addr, offset)) {
            _timers.store(offset, val);

            return;
        }

        if (map::CDROM.contains(abs_addr, offset)) {
            _cdrom.store(offset, val);
            //psx_cdrom_write8(_cdrom, offset, val);

            return;
        }

        if (map::MDEC.contains(abs_addr, offset)) {
            mdec.store(offset, val);

            return;
        }

        if (map::SPU.contains(abs_addr, offset)) {
            //throw std::runtime_error(": 0x" + to_hex(offset) + " <- 0x" + to_hex(val));

            _spu->write(offset, (val) & 0xFF);
            _spu->write(offset + 1, (val >> 8) & 0xFF);

            /*if constexpr (sizeof(T) == 1) {
                uint32_t regAddr = abs_addr & ~1u;
                uint16_t reg = static_cast<uint16_t>(spu.load(regAddr));
                uint16_t byteVal = static_cast<uint8_t>(val);
                
                if (abs_addr & 1u) {
                    reg = static_cast<uint16_t>((reg & 0x00FF) | (byteVal << 8));
                } else {
                    reg = static_cast<uint16_t>((reg & 0xFF00) | byteVal);
                }
                
                spu.store(regAddr, reg);
            } else if constexpr (sizeof(T) == 2) {
                spu.store(abs_addr & ~1u, static_cast<uint16_t>(val));
            } else if constexpr (sizeof(T) == 4) {
                spu.store(abs_addr & ~1u, static_cast<uint16_t>(val & 0xFFFF));
                spu.store((abs_addr & ~1u) + 2, static_cast<uint16_t>((val >> 16) & 0xFFFF));
            }*/
            
            return;
        }
        
        // Peripheral I/O Ports
        if (map::PADMEMCARD.contains(abs_addr, offset)) {
            _sio.store(abs_addr, val, sizeof(T));
            
            return;
        }
        
        if (auto _ = map::CACHECONTROL.contains(abs_addr, offset)) {
            if (sizeof(T) != 4) {
                throw std::runtime_error("Unhandled cache control access");
            }
            
            _cacheControl.val = val;
            
            return;
        }
        
        if (map::MEMCONTROL.contains(abs_addr, offset)) {
            if (sizeof(T) != 4) {
                throw std::runtime_error("Unbalanced MEM_CONTROL access (" + std::to_string(sizeof(T)) + ")");
            }
            
            uint32_t index = (offset >> 2);
            memControl[index] = val;
            
            return;
        }
        
        if (auto _ = map::RAM_SIZE.contains(abs_addr, offset)) {
            if (sizeof(T) != 4) {
                throw std::runtime_error("Unhandled RAM_SIZE access");
            }
            
            _ramSize = val;
            
            return;
        }

        if (auto _ = map::EXPANSION1.contains(abs_addr, offset)) {
            if(_ == 132) {
                //   1F000084h 2Ch Pre-Boot ID  ("Licensed by Sony Computer Entertainment Inc.")
                return;
            }

            throw std::runtime_error("Unhandled EXPANSION1 load at address 0x");
        }
        
        if (map::EXPANSION2.contains(abs_addr, offset)) {
            // TTY
            //if(offset == 0x23) {
                std::cerr << static_cast<char>(val) << "";
            //}
            
            return;
        }

        // TOOD: ???
        if (addr == 0xBFC00000) {
            return;
        }

        if (addr == 0x1FA00000) {
            // Expansion Region 3 (default 1 byte, max 2 MBytes)

            /**
             * 1FA00000h - Not used by BIOS or any PSX games
             * 1FA00000h - POST3 (similar to POST, but PS2 BIOS uses this address)
             */

            return;
        }
        
        throw std::runtime_error("Unhandled store into address 0x");
        return;
    }

    bool dcacheFillEnabled() const {
        uint32_t biu = _cacheControl.val;

        return (biu & (1u << 7)) && !(biu & (1u << 3));
    }

   void dcacheFill(uint32_t addr, uint32_t value) {
        if (!dcacheFillEnabled()) {
            return;
        }

        if (addr < 0x80000000 || addr >= 0xa0000000) {
            return;
        }

        _scratchPad.store<uint32_t>(((addr >> 2) & 0xff) * 4, value);
    }
    
    // DMA register read
    uint32_t dmaReg(uint32_t offset);
    
    // Execute DMA transfer for a port
    void doDma(Port port);
    void dmaBlock(Port port);
    void dmaLinkedList(Port port);
    void setDmaReg(uint32_t offset, uint32_t val);
    
    void reset() {
        expansion1Base = 0;
        expansion2Base = 0;
        
        _ramSize = 0;
        
        for(unsigned int& i : memControl)
            i = 0;
        
        _ram.reset();
        _cdrom.reset();
        _scratchPad.reset();
        _irq.reset();
        _timers.reset();
        mdec.reset();
        
        _cacheControl = (0);
        for(auto& i : icache)
            i = {};

        // TODO;
        _bios.reset("../../BIOS/ps-22a.bin");
        _dma.reset();
        _gpu->reset();
        //spu.reset();

        scheduler.reset();
        _hardwareTickScheduled = false;
    }
    
private:
    uint32_t expansion1Base = 0;
    uint32_t expansion2Base = 0;
    
    uint32_t _ramSize = 0;
    
    uint32_t memControl[9];
    
public:
    bool lastICacheMiss = false;

    bool flatMemoryMode = false;
    std::unordered_map<uint32_t, uint8_t> flatMemory;

    Ram _ram;
    //CDROM _cdrom;
    CDROM _cdrom;
    //cdrom _cdrom;
    //psx_cdrom_t* _cdrom;
    ScratchPad _scratchPad;
    //Joypad _joypad;
    Emulator::IO::SIO _sio;
    
    IRQ _irq;
    
    Emulator::IO::Timers _timers;
    
    CacheControl _cacheControl = (0);

    // The i-Cache can hold 4096 bytes, or 1024 instructions.
    ICache icache[256];

    Bios _bios;
    Dma _dma;
    Emulator::Gpu* _gpu;
    MDEC mdec;
    spu::SPU* _spu;
    Emulator::SPU spu;

    Scheduler scheduler;

    bool _hardwareTickScheduled = false;
    bool _vblankPending = false;
    uint64_t _lastHardwareTickCycle = 0;
};
