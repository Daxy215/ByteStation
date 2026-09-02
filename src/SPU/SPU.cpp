#include "SPU.h"

#include <cassert>
#include <iostream>

#include <cmath>
#include <sstream>
#include <chrono>

#include "Memory/IRQ.h"
#include "Memory/CDROM/CDROM.h"

#define M_PI 3.14159265358979323846

// Windows being gay
typedef unsigned   uint32_t;

#define LOG

Emulator::SPU::SPU () {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        printf("Failed to init SDL audio: %s\n", SDL_GetError());
    }

    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = nullptr;

    device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);

    if (device == 0) {
        printf("Failed to open audio: %s\n", SDL_GetError());
    } else {
        printf("SDL audio driver: %s\n", SDL_GetCurrentAudioDriver());
        printf("SDL audio device opened: freq=%d format=0x%04x channels=%u samples=%u\n",
               have.freq,
               have.format,
               have.channels,
               have.samples);
        SDL_PauseAudioDevice(device, 0);
    }
}

static uint32_t curCycles = 0;
static const int AUDIO_BUFFER_SIZE = 28 * 2 * 4;//1024 * 2;
int16_t audioBuffer[AUDIO_BUFFER_SIZE];
int audioIndex = 0;

void Emulator::SPU::step(uint32_t cycles) {
    /**
     * TODO:
     * It seems like its meant to do this per cycle..
     * And not everything at once
     *
     * https://psx-spx.consoledev.net/soundprocessingunitspu/#notes_1
     */

    curCycles += cycles;
    
    stepTransfer();
    
    // Every 768 CPU cycles -> ~44.1 kHz
    const int CYCLES_PER_SAMPLE = 768; //768
    
    while (curCycles >= CYCLES_PER_SAMPLE) {
        curCycles -= CYCLES_PER_SAMPLE;

        int32_t left = 0;
        int32_t right = 0;
        int32_t cdLeft = 0;
        int32_t cdRight = 0;

        if (!CDROM::audioSamples.empty()) {
            auto sample = CDROM::audioSamples.front();
            CDROM::audioSamples.pop_front();

            cdLeft = sample.first;
            cdRight = sample.second;
        }

        auto writeData = [this, cdLeft, cdRight]() {
            uint32_t idx = bufferIndex & 0x1FF;

            writeToRAM16(0x0000 + idx, cdLeft);
            writeToRAM16(0x0200 + idx, cdRight);
            writeToRAM16(0x0400 + idx, voices[1].currentSample);
            writeToRAM16(0x0600 + idx, voices[3].currentSample);
        };

        int32_t noiseStep = 4 + spunct.Noise_Freq_Step;
        int32_t noiseShift = spunct.Noise_Freq_Shift;

        noiseTimer -= noiseStep;

        while (noiseTimer < 0) {
            noiseTimer += 0x20000 >> noiseShift;

            int32_t parity = ((noiseLevel >> 15) ^ (noiseLevel >> 12) ^ (noiseLevel >> 11) ^ (noiseLevel >> 10) ^ 1) & 1;

            noiseLevel = static_cast<int16_t>((noiseLevel << 1) | parity);
        }
        
        for (int i = 0; i < VOICE_COUNT; i++) {
            Voice &voice = voices[i];

            /**
             * Voice 1
             * Write CD Left
             * Write CD Right
             * Write Voice 1
             * Write Voice 3
             * Reveb
             * Reset of the Voices
             */
            /*if (i == 1) {
                writeData();
            }*/

            if (voice.adsr.state == ADSR::Off)
                continue;
            
            voice.step(i, i > 0 ? voices[i - 1].currentSample : 0, *this);

            if (voice.muted)
                continue;
            
            left  += voice.currentLeft;
            right += voice.currentRight;
        }
        
        if (spunct.CD_Audio_Enable) {
            left += (cdLeft * cdInputVolLeft) >> 15;
            right += (cdRight * cdInputVolRight) >> 15;
        }

        left = (left * static_cast<int16_t>(mainValLeft)) >> 15;
        right = (right * static_cast<int16_t>(mainValRight)) >> 15;

        if (!audioEnabled) {
            left  = 0;
            right = 0;
        } else {
            left  = static_cast<int32_t>(left  * masterVolume);
            right = static_cast<int32_t>(right * masterVolume);
        }

        audioBuffer[audioIndex++] = static_cast<int16_t>(std::clamp<int32_t>(left,  -32768, 32767));
        audioBuffer[audioIndex++] = static_cast<int16_t>(std::clamp<int32_t>(right, -32768, 32767));

        writeData();

        bufferIndex = (bufferIndex + 1) & 0x1FF;
        status.Capture_Buffer_Half = (bufferIndex >= 0x100) ? 1 : 0;

        if (audioIndex >= AUDIO_BUFFER_SIZE) {
            if (device != 0) {
                SDL_QueueAudio(device, audioBuffer, sizeof(audioBuffer));
                queuedBufferCount++;
            }
            
            audioIndex = 0;
        }
    }
}

void Emulator::SPU::stepTransfer() {
    uint8_t mode = (spunct.Sound_RAM_Transfer) & 0x3;

    if ((mode != 1 && mode != 2) || buffer.empty()) {
        status.Data_Transfer_Busy_Flag = 0;

        return;
    }

    status.Data_Transfer_Busy_Flag = 1;

    while (!buffer.empty()) {
        const uint8_t type = (transferControl >> 1) & 0x7;

        uint16_t poppedData;
        buffer.pop(poppedData);

        switch (type) {
            case 2: // Normal
                transferCache = poppedData;

                break;

            case 3: { // Rep2
                if ((transferIndex & 1) == 0) transferCache = poppedData;
                transferIndex = (transferIndex + 1) & 1;

                break;
            }

            case 4: { // Rep4
                if ((transferIndex & 3) == 0) transferCache = poppedData;
                transferIndex = (transferIndex + 1) & 3;

                break;
            }

            case 5: { // Rep8
                if ((transferIndex & 7) == 7) transferCache = poppedData;
                transferIndex = (transferIndex + 1) & 7;

                break;
            }

            default: { // Fill
                transferCache = poppedData;

                break;
            }
        }

        uint32_t ramIdx = (currentAddress >> 1) & 0x3FFFF;
        writeToRAM16(ramIdx, transferCache);

        currentAddress = (currentAddress + 2) & 0x7FFFF;
    }
}

uint32_t Emulator::SPU::load(uint32_t addr) {
    addr &= ~1u;

    /**
     * 1F801C00h..1F801D7Fh - Voice 0..23 Registers (eight 16bit regs per voice)
     * 1F801D80h..1F801D87h - SPU Control (volume)
     * 1F801D88h..1F801D9Fh - Voice 0..23 Flags (six 1bit flags per voice)
     * 1F801DA2h..1F801DBFh - SPU Control (memory, control, etc.)
     * 1F801DC0h..1F801DFFh - Reverb configuration area
     * 1F801E00h..1F801E5Fh - Voice 0..23 Internal Registers
     * 1F801E60h..1F801E7Fh - Unknown?
     * 1F801E80h..1F801FFFh - Unused?
     */
    if (addr >= 0x1F801C00 && addr <= 0x1F801D7F) {
        // Voice 0..23 Registers (eight 16-bit regs per voice)
        return handleVoiceLoad(addr);
    } else if (addr >= 0x1F801D80 && addr <= 0x1F801D87) {
        // SPU Control (volume)
        return handleVolumeLoad(addr);
    } else if (addr >= 0x1F801D88 && addr <= 0x1F801D9F) {
        // Voice 0..23 Flags (six 1-bit flags per voice)
        return handleFlagsLoad(addr);
    } else if (addr >= 0x1F801DA2 && addr <= 0x1F801DBF) {
        // SPU Control (memory, control, etc.)
        return handleControlLoad(addr);
    } else if (addr >= 0x1F801DC0 && addr <= 0x1F801DFF) {
        // Reverb configuration area
        return 0;
    } else if (addr >= 0x1F801E00 && addr <= 0x1F801E5F) {
        // Voice 0..23 Internal Registers
        return handleVoiceInternalLoad(addr);
    } else if (addr >= 0x1F801E60 && addr <= 0x1F801E7F) {
        // Unknown
        return 0;
    } else if (addr >= 0x1F801E80 && addr <= 0x1F801FFF) {
        // Unused
        return 0;
    }
    
    #ifdef LOG
        std::cerr << "Unhandled load from SPU register; " << std::hex << (addr) << "\n";
    #endif
    
    return 0;
}

void Emulator::SPU::store(uint32_t addr, uint32_t val) {
    addr &= ~1u;

    /**
     * 1F801C00h..1F801D7Fh - Voice 0..23 Registers (eight 16bit regs per voice)
     * 1F801D80h..1F801D87h - SPU Control (volume)
     * 1F801D88h..1F801D9Fh - Voice 0..23 Flags (six 1bit flags per voice)
     * 1F801DA2h..1F801DBFh - SPU Control (memory, control, etc.)
     * 1F801DC0h..1F801DFFh - Reverb configuration area
     * 1F801E00h..1F801E5Fh - Voice 0..23 Internal Registers
     * 1F801E60h..1F801E7Fh - Unknown?
     * 1F801E80h..1F801FFFh - Unused?
     */
    if (addr >= 0x1F801C00 && addr <= 0x1F801D7F) {
        // Voice 0..23 Registers (eight 16-bit regs per voice)
        handleVoiceStore(addr, val);
        
        return;
    } else if (addr >= 0x1F801D80 && addr <= 0x1F801D87) {
        // SPU Control (volume)
        handleVolumeStore(addr, val);
        
        return;
    } else if (addr >= 0x1F801D88 && addr <= 0x1F801D9F) {
        // Voice 0..23 Flags (six 1-bit flags per voice)
        handleFlagsStore(addr , val);
        
        return;
    } else if (addr >= 0x1F801DA2 && addr <= 0x1F801DBF) {
        // SPU Control (memory, control, etc.)
        handleControlStore(addr, val);
        
        return;
    } else if (addr >= 0x1F801DC0 && addr <= 0x1F801DFF) {
        // Reverb configuration area
        return;
    } else if (addr >= 0x1F801E00 && addr <= 0x1F801E5F) {
        // Voice 0..23 Internal Registers
    } else if (addr >= 0x1F801E60 && addr <= 0x1F801E7F) {
        // Unknown
    } else if (addr >= 0x1F801E80 && addr <= 0x1F801FFF) {
        // Unused
    }
    
    #ifdef LOG
        std::cerr << "Unhandled store from SPU register; " << std::hex << (addr) << "\n";
    #endif
}

uint32_t Emulator::SPU::handleVoiceLoad(const uint32_t addr) {
    const uint32_t voiceRegister = addr - 0x1F801C00;
    const uint8_t voiceIndex = voiceRegister / 0x10;
    const uint8_t offset = voiceRegister & 0x0F;
    
    switch (offset) {
        case 0x00: {
            // 1F801C00h+N*10h - Voice 0..23 Volume Left
            return voices[voiceIndex].adsr.volLeft.reg;
        }

        case 0x02: {
            // 1F801C02h+N*10h - Voice 0..23 Volume Right
            return voices[voiceIndex].adsr.volRight.reg;
        }

        case 0x04: {
            // 1F801C04h+N*10h - Voice 0..23 ADPCM Sample Rate
            return voices[voiceIndex].adpcm.vxPitch & 0xFFFF;
        }

        case 0x06: {
            // 1F801C06h+N*10h - Voice 0..23 ADPCM Start Address
            return voices[voiceIndex].startAddress & 0xFFFF;
        }

        case 0x08: {
            // 1F801C08h+N*10h - Voice ADSR lower 16 bits
            return voices[voiceIndex].adsr.adsr & 0xFFFF;
        }

        case 0x0A: {
            // 1F801C0Ah+N*10h - Voice ADSR upper 16 bits
            return (voices[voiceIndex].adsr.adsr >> 16) & 0xFFFF;
        }

        case 0x0C: {
            // 1F801C0Ch+N*10h - Voice 0..23 Current ADSR volume (R/W)
            //   15-0  Current ADSR Volume  (0..+7FFFh) (or -8000h..+7FFFh on manual write)
            return voices[voiceIndex].adsr.currentVolume;
        }

        case 0x0E: {
            // 1F801C0Eh+N*10h - Voice repeat address
            return (voices[voiceIndex].repeatAddress / 4) & 0xFFFF;
        }

        default: {
            #ifdef LOG
                std::cerr << "Unhandled voice load from SPU register; " << std::hex << (addr) << "\n";
            #endif
            
            break;
        }
    }

    return 0;
}

void Emulator::SPU::handleVoiceStore(uint32_t addr, uint32_t val) {
    const uint32_t voiceRegister = addr - 0x1F801C00;
    const uint8_t voiceIndex = voiceRegister / 0x10;
    const uint8_t offset = voiceRegister & 0x0F;
    
    switch(offset) {
        case 0x00: 
            // 1F801C00h+N*10h - Voice 0..23 Volume Left
            voices[voiceIndex].adsr.volLeft.write(val & 0xFFFF);
            
            break;
        case 0x02:
            // 1F801C02h+N*10h - Voice 0..23 Volume Right
            voices[voiceIndex].adsr.volRight.write(val & 0xFFFF);
            
            break;
        case 0x04:
            // 1F801C04h+N*10h - Voice 0..23 ADPCM Sample Rate (R/W) (VxPitch)
            //   0-15  Sample rate (0=stop, 4000h=fastest, 4001h..FFFFh=usually same as 4000h)
            
            voices[voiceIndex].adpcm.vxPitch = val & 0xFFFF;
            
            break;
        case 0x06:
            // 1F801C06h+N*10h - Voice 0..23 ADPCM Start Address (R/W)

            // N * 8 -> Phyiscal byte address inside soundRAM
            voices[voiceIndex].startAddress = (val & 0xFFFF);
            
            break;
        case 0x08:
            // 1F801C08h+N*10h - Voice 0..23 Attack/Decay/Sustain/Release (ADSR) (32bit)
            
            /**
            *   ____lower 16bit (at 1F801C08h+N*10h)___________________________________
            * 15    Attack Mode       (0=Linear, 1=Exponential)
            * -     Attack Direction  (Fixed, always Increase) (until Level 7FFFh)
            * 14-10 Attack Shift      (0..1Fh = Fast..Slow)
            * 9-8   Attack Step       (0..3 = "+7,+6,+5,+4")
            * -     Decay Mode        (Fixed, always Exponential)
            * -     Decay Direction   (Fixed, always Decrease) (until Sustain Level)
            * 7-4   Decay Shift       (0..0Fh = Fast..Slow)
            * -     Decay Step        (Fixed, always "-8")
            * 3-0   Sustain Level     (0..0Fh)  ;Level=(N+1)*800h
            */
            
            voices[voiceIndex].adsr.adsr = (voices[voiceIndex].adsr.adsr & 0xFFFF0000) |(val & 0xFFFF);            
            
            break;
        case 0x0A:
            // ADSR - upper 16bit (at 1F801C0Ah+N*10h)
            
            /**
             *  ____upper 16bit (at 1F801C0Ah+N*10h)___________________________________
             *  31    Sustain Mode      (0=Linear, 1=Exponential)
             *  30    Sustain Direction (0=Increase, 1=Decrease) (until Key OFF flag)
             *  29    Not used?         (should be zero)
             *  28-24 Sustain Shift     (0..1Fh = Fast..Slow)
             *  23-22 Sustain Step      (0..3 = "+7,+6,+5,+4" or "-8,-7,-6,-5") (inc/dec)
             *  21    Release Mode      (0=Linear, 1=Exponential)
             *  -     Release Direction (Fixed, always Decrease) (until Level 0000h)
             *  20-16 Release Shift     (0..1Fh = Fast..Slow)
             *  -     Release Step      (Fixed, always "-8")
             */
            
            voices[voiceIndex].adsr.adsr = (voices[voiceIndex].adsr.adsr & 0x0000FFFF) | ((val & 0xFFFF) << 16);
            
            break;
        case 0x0C:
            // 1F801C0Ch+N*10h - Voice 0..23 Current ADSR volume (R/W)
            voices[voiceIndex].adsr.currentVolume = static_cast<int16_t>(val & 0xFFFF);

            break;
        case 0x0E:
            // 1F801C0Eh+N*10h - Voice repeat address.
            voices[voiceIndex].repeatAddress = (val & 0xFFFF) * 4;
            
            break;
        default: {
            #ifdef LOG
                std::cerr << "Unhandled voice store from SPU register; " << std::hex << (addr) << "\n";
            #endif
            
            break;
        }
    }
}

uint32_t Emulator::SPU::handleVolumeLoad(uint32_t addr) {
    switch(addr) {
        case 0x1F801D80: {
            // 1F801D80h - Mainvolume left
            return mainValLeft;
        }
        
        case 0x1F801D82: {
            // 1F801D82h - Mainvolume right
            return mainValRight;
        }
        
        case 0x1f801d84: {
            // 1F801D84h spu   vLOUT   volume  Reverb Output Volume Left
            return vLOUT;
        }
        
        case 0x1f801d86: {
            //   1F801D86h spu   vROUT   volume  Reverb Output Volume Right
            return vROUT;
        }
        
        default: {
            #ifdef LOG
                std::cerr << "Unhandled volume read from SPU register; " << std::hex << (addr) << "\n";
            #endif
            
            break;
        }
    }

    return 0;
}

void Emulator::SPU::handleVolumeStore(const uint32_t addr, const uint32_t val) {
    switch(addr) {
        case 0x1f801D80: {
            // 1F801D80h - Mainvolume left
            mainValLeft = (val & 0xFFFF);

            break;
        }

        case 0x1f801D82: {
            // 1F801D82h - Mainvolume right
            mainValRight = (val & 0xFFFF);

            break;
        }
        
        case 0x1f801D84: {
            // 1F801D84h spu   vLOUT   volume  Reverb Output Volume Left
            vLOUT = (val & 0xFFFF);
            
            break;
        }
        
        case 0x1f801D86: {
            //   1F801D86h spu   vROUT   volume  Reverb Output Volume Right
            vROUT = (val & 0xFFFF);
            
            break;
        }
        
        default: {
            #ifdef LOG
                std::cerr << "Unhandled volume store from SPU register; " << std::hex << (addr) << "\n";
            #endif
            
            break;
        }
    }
}

uint32_t Emulator::SPU::handleFlagsLoad(uint32_t addr) {
    switch (addr) {
        case 0x1F801D88: return KON & 0xFFFF;
        case 0x1F801D8A: return (KON >> 16) & 0xFF;
        case 0x1f801D8C: return KOFF & 0xFFFF;
        case 0x1f801D8E: return (KOFF >> 16) & 0xFF;
        case 0x1f801D98: return EON & 0xFFFF;
        case 0x1F801D9A: return (EON >> 16) & 0xFF;
        case 0x1F801D9C: return ENDX & 0xFFFF;
        case 0x1F801D9E: return (ENDX >> 16) & 0xFF;
        default: {
            #ifdef LOG
                std::cerr << "Unhandled Voice Flags Load from SPU register; " << std::hex << (addr) << "\n";
            #endif
            
            break;
        }
    }
    
    return 0;
}

void Emulator::SPU::handleFlagsStore(uint32_t addr, uint32_t val) {
    uint16_t v = val & 0xFFFF;
    
    switch(addr) {
        // 1F801D88h - Voice 0..23 Key ON (KON)
        case 0x1F801D88: { // low 16
            for(int i = 0; i < 16; i++) {
                if(spunct.SPU_Enable && (v >> i) & 1) {
                    voices[i].triggerKeyOn(i, curCycles, *this);
                }
            }

            KON = (KON & 0xFFFF0000) | v;
            
            break;
        }
        case 0x1F801D8A: { // high 16
            for(int i = 0; i < 8; i++) {
                if(spunct.SPU_Enable && (v >> i) & 1) {
                    voices[i + 16].triggerKeyOn(i + 16, curCycles, *this);
                }
            }

            KON = (KON & 0xFFFF) | ((v & 0xFF) << 16);
            
            break;
        }
        
        // 1F801D8Ch - Voice 0..23 Key OFF (KOFF)
        case 0x1F801D8C: { // low 16
            for(int i = 0; i < 16; i++) {
                if(spunct.SPU_Enable &&(v >> i) & 1) {
                    voices[i].triggerKeyOff(curCycles);
                }
            }

            KOFF = (KOFF & 0xFFFF0000) | v;
            
            break;
        }
        case 0x1F801D8E: { // high 16
            for(int i = 0; i < 8; i++) {
                if(spunct.SPU_Enable && (v >> i) & 1) {
                    voices[i + 16].triggerKeyOff(curCycles);
                }
            }

            KOFF = (KOFF & 0xFFFF) | ((v & 0xFF) << 16);
            
            break;
        }
        
        // 1F801D90h - Pitch Modulation Enable Flags (PMON)
        case 0x1F801D90: { // low 16
            PMON = (PMON & 0xFFFF0000) | v;

            for (int i = 0; i < 16; i++) {
                voices[i].PMON = (PMON >> i) & 1;
            }

            break;
        }
        case 0x1F801D92: { // high 16
            PMON = (PMON & 0x0000FFFF) | (v << 16);

            for (int i = 0; i < 8; i++) {
                voices[i+16].PMON = (PMON >> (i+16)) & 1;
            }

            break;
        }
        
        // 1F801D94h - Noise mode enable (NON)
        case 0x1F801D94: { // low 16
            NON = (NON & 0xFFFF0000) | v;

            break;
        }
        case 0x1F801D96: { // high 16
            NON = (NON & 0x0000FFFF) | (v << 16);

            break;
        }
        
        // 1F801D98h - Reverb mode / Echo On (EON)
        case 0x1F801D98: { // low 16
            EON = (EON & 0xFFFF0000) | v;

            break;
        }
        case 0x1F801D9A: { // high 16
            EON = (EON & 0x0000FFFF) | (v << 16);

            break;
        }
        case 0x1F801D9C:
        case 0x1F801D9E: {
            break;
        }
        default: {
            #ifdef LOG
                std::cerr << "Unhandled flags store from SPU register; "
                          << std::hex << addr << "\n";
            #endif

            break;
        }
    }
}

uint32_t Emulator::SPU::handleControlLoad(uint32_t addr) {
    switch(addr) {
        case 0x1F801DA2: {
            //   1F801DA2h spu   mBASE   base    Reverb Work Area Start Address in Sound RAM
            // TODO;
            return 0;
        }
        case 0x1F801DA4: {
            // 1F801DA4h - Sound RAM IRQ Address (IRQ9)
            return irqAddress;
        }
        case 0x1F801DA6: {
            // 1F801DA6h - Sound RAM Data Transfer Address
            return transferAddress;
        }

        case 0x1F801DB8: {
            // 1F801DB8h - Current Main Volume Left/Right
            return mainValLeft;
        }

        case 0x1F801DBA: {
            // 1F801DB8h - Current Main Volume Left/Right
            return mainValRight;
        }

        case 0x1F801DAA: {
            // 1F801DAAh - SPU Control Register (SPUCNT)
            return spunct._reg;
        }
        
        case 0x1F801DAE: {
            // 1F801DAEh - SPU Status Register (SPUSTAT) (R)
            return status._reg;
        }
        
        case 0x1F801DAC: {
            // 1F801DACh - Sound RAM Data Transfer Control (should be 0004h)
            return transferControl;
        }
        
        case 0x1F801DB0: {
            return static_cast<uint16_t>(cdInputVolLeft);
        }
        
        case 0x1F801DB2: {
            return static_cast<uint16_t>(cdInputVolRight);
        }
        
        case 0x1F801DB4: {
            return static_cast<uint16_t>(exVolLeft);
        }
        
        case 0x1F801DB6: {
            return static_cast<uint16_t>(exVolRight);
        }
        
        default: {
            #ifdef LOG
                std::cerr << "Unhandled control store from SPU register; " << std::hex << (addr) << "\n";
            #endif
            
            break;
        }
    }
    
    return 0;
}

void Emulator::SPU::handleControlStore(uint32_t addr, uint32_t val) {
    switch(addr) {
        case 0x1F801DA2: {
            // ??
            break;
        }
        case 0x1F801DA4: {
            // 1F801DA4h - Sound RAM IRQ Address (IRQ9)
            irqAddress = (val & 0xFFFF);
            break;
        }
        case 0x1F801DA6: {
            // 1F801DA6h - Sound RAM Data Transfer Address
            transferAddress = (val & 0xFFFF);
            currentAddress = transferAddress * 8;
            
            break;
        }
        
        case 0x1F801DA8: {
            buffer.push(static_cast<uint16_t>(val & 0xFFFF));

            break;
        }
        
        case 0x1F801DAA: {
            // 1F801DAAh - SPU Control Register (SPUCNT)
            spunct._reg = val;

            // Bits 0-5: Current SPU Mode (delayed version of SPUCNT.Bit5-0)
            status.Current_SPU_Mode = spunct._reg & 0x3F;
            
            if (!spunct.SPU_Enable) {
                for (auto& v : voices) {
                    v.adsr.currentVolume = 0;
                }
            }

            break;
        }
        
        case 0x1f801DAE: {
            // 1F801DAEh - SPU Status Register (SPUSTAT) (R)
            // Read-only?
            //status._reg = val;
            
            throw std::runtime_error("[ERROR] [SPU] Attempt to write to a read-only address!");
            
            break;
        }
        
        /*case 0x1f801DB0: {
            // 1F801DB0h - CD Audio Input Volume (for normal CD-DA, and compressed XA-ADPCM)
            cdInputVol = val;
            
            break;
        }*/

        case 0x1F801DB0: {
            cdInputVolLeft = static_cast<int16_t>(val & 0xFFFF);
            break;
        }

        case 0x1F801DB2: {
            cdInputVolRight = static_cast<int16_t>(val & 0xFFFF);
            break;
        }
        
        case 0x1F801DB4: {
            exVolLeft = static_cast<int16_t>(val & 0xFFFF);

            break;
        }

        case 0x1F801DB6: {
            exVolRight = static_cast<int16_t>(val & 0xFFFF);
            
            break;
        }

        case 0x1F801DB8: {
            // 1F801DB8h - Current Main Volume Left/Right
            // TODO: what?
            break;
        }
        
        case 0x1F801DAC: {
            // 1F801DACh - Sound RAM Data Transfer Control (should be 0004h)

            transferControl = val;

            //assert(val == 4);

            /**
             * 15-4   Unknown/no effect?                       (should be zero)
             * 3-1    Sound RAM Data Transfer Type (see below) (should be 2)
             * 0      Unknown/no effect?                       (should be zero)
             * The Transfer Type selects how data is forwarded from Fifo to SPU RAM:
             *   __Transfer Type___Halfwords in Fifo________Halfwords written to SPU RAM__
             *   0,1,6,7  Fill     A,B,C,D,E,F,G,H,...,X    X,X,X,X,X,X,X,X,...
             *   2        Normal   A,B,C,D,E,F,G,H,...,X    A,B,C,D,E,F,G,H,...
             *   3        Rep2     A,B,C,D,E,F,G,H,...,X    A,A,C,C,E,E,G,G,...
             *   4        Rep4     A,B,C,D,E,F,G,H,...,X    A,A,A,A,E,E,E,E,...
             *   5        Rep8     A,B,C,D,E,F,G,H,...,X    H,H,H,H,H,H,H,H,...
             * Rep2 skips the 2nd halfword, Rep4 skips 2nd..4th, Rep8 skips 1st..7th.
             * Fill uses only the LAST halfword in Fifo, that might be useful for memfill purposes, although, the length is probably determined by the number of writes to the Fifo (?) so one must still issue writes for ALL halfwords...?
             * Note:
             * The above rather bizarre results apply to WRITE mode. In READ mode, the register causes the same halfword to be read 2/4/8 times (for rep2/4/8).
             */

            break;
        }

        default: {
            #ifdef LOG
                std::cerr << "Unhandled control store from SPU register; " << std::hex << (addr) << "\n";
            #endif
            
            break;
        }
    }
}

uint32_t Emulator::SPU::handleVoiceInternalLoad(uint32_t addr) {
    switch (addr) {
        default: {
            #ifdef LOG
                std::cerr << "Unhandled Voice Internal Load from SPU register; " << std::hex << (addr) << "\n";
            #endif
            
            break;
        }
    }
    
    return 0;
}

void Emulator::SPU::handleVoiceInternalStore(uint32_t addr, uint32_t val) {
        switch (addr) {
            default: {
                #ifdef LOG
                    std::cerr << "Unhandled Voice Internal Store from SPU register; " << std::hex << (addr) << "\n";
                #endif

                break;
            }
    }
}

void Emulator::SPU::writeToRAM8(uint32_t addr, uint8_t val) {
    uint32_t idx = (addr >> 1) & 0x3FFFF;

    if (addr & 1) {
        soundRAM[idx] = (soundRAM[idx] & 0x00FF) | (val << 8);
    } else {
        soundRAM[idx] = (soundRAM[idx] & 0xFF00) | val;
    }

    if (spunct.IRQ9_Enable && idx == static_cast<uint32_t>(irqAddress) * 4) {
        status.IRQ9_Flag = true;
        IRQ::trigger(IRQ::Interrupt::SPU);
    }
}

uint8_t Emulator::SPU::readFromRAM8(uint32_t addr) {
    uint32_t idx = (addr >> 1) & 0x3FFFF;

    return (addr & 1) ? (soundRAM[idx] >> 8) & 0xFF : soundRAM[idx] & 0xFF;
}

void Emulator::SPU::writeToRAM16(uint32_t addr, uint16_t val) {
    uint32_t idx = addr & 0x3FFFF;

    soundRAM[idx] = val;

    if (spunct.IRQ9_Enable && addr == irqAddress * 4) {
        status.IRQ9_Flag = true;
        IRQ::trigger(IRQ::Interrupt::SPU);
    }
}

uint16_t Emulator::SPU::readFromRAM16(uint32_t addr) {
    return soundRAM[addr & 0x3FFFF];
}

void Emulator::SPU::pushSamples(int16_t* samples, int sampleCount) {
    SDL_QueueAudio(device, samples, sampleCount * sizeof(int16_t) * 2);
}
