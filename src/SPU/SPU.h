#pragma once

#include <cstdio>
#include <stdint.h>
#include <deque>
#include <vector>
#include <utility>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <algorithm>
#include <functional>

namespace Emulator {
    class SPU;

    struct Fifo {
        static constexpr uint32_t SIZE = 65536; // 32
        static constexpr uint32_t MASK = SIZE - 1;

        std::vector<uint16_t> buffer = std::vector<uint16_t>(SIZE, 0);
        uint32_t write_pos = 0;
        uint32_t read_pos  = 0;
        uint32_t count     = 0;

        bool push(uint16_t value) {
            if (count == SIZE)
                return false; // full

            buffer[write_pos] = value;
            write_pos = (write_pos + 1) & MASK;
            ++count;

            return true;
        }

        bool pop(uint16_t& value) {
            if (count == 0)
                return false; // empty

            value = buffer[read_pos];
            read_pos = (read_pos + 1) & MASK;
            --count;

            return true;
        }

        bool full() const  { return count == SIZE; }
        bool empty() const { return count == 0; }

        void reset() {
            write_pos = 0;
            read_pos  = 0;
            count     = 0;
        }
    };

    // https://psx-spx.consoledev.net/soundprocessingunitspu/#1f801daah-spu-control-register-spucnt
    union Status {
        struct {
            uint16_t Current_SPU_Mode          : 6; // Bits 0-5: Current SPU Mode (delayed version of SPUCNT.Bit5-0)
            uint16_t IRQ9_Flag                 : 1; // Bit 6: IRQ9 Flag (0=No, 1=Interrupt Request)
            uint16_t DMA_Read_Write_Request    : 1; // Bit 7: Data Transfer DMA Read/Write Request (same as SPUCNT.Bit5)
            uint16_t DMA_Write_Request         : 1; // Bit 8: Data Transfer DMA Write Request (0=No, 1=Yes)
            uint16_t DMA_Read_Request          : 1; // Bit 9: Data Transfer DMA Read Request (0=No, 1=Yes)
            uint16_t Data_Transfer_Busy_Flag   : 1; // Bit 10: Data Transfer Busy Flag (0=Ready, 1=Busy)
            uint16_t Capture_Buffer_Half       : 1; // Bit 11: Writing to Capture Buffers (0=First, 1=Second half)
            uint16_t Unused                    : 4; // Bits 12-15: Unknown/Unused (usually zero)
        };

        uint16_t _reg;

        Status() = default;
        Status(uint32_t reg) : _reg(reg) {

        }
    };

    // SPU Control Register
    union Spucnt {
        struct {
            uint16_t CD_Audio_Enable        : 1;  // 0     CD Audio Enable         (0=Off, 1=On) (for CD-DA and XA-ADPCM)
            uint16_t External_Audio_Enable  : 1;  // 1     External Audio Enable   (0=Off, 1=On)
            uint16_t CD_Audio_Reverb        : 1;  // 2     CD Audio Reverb         (0=Off, 1=On) (for CD-DA and XA-ADPCM)
            uint16_t External_Audio_Reverb  : 1;  // 3     External Audio Reverb   (0=Off, 1=On)
            uint16_t Sound_RAM_Transfer     : 2;  // 5-4   Sound RAM Transfer Mode (0=Stop, 1=ManualWrite, 2=DMAwrite, 3=DMAread)
            uint16_t IRQ9_Enable            : 1;  // 6     IRQ9 Enable (0=Disabled/Acknowledge, 1=Enabled; only when Bit15=1)
            uint16_t Reverb_Master_Enable   : 1;  // 7     Reverb Master Enable    (0=Disabled, 1=Enabled)
            uint16_t Noise_Freq_Step        : 2;  // 9-8   Noise Frequency Step    (0..03h = Step "4,5,6,7")
            uint16_t Noise_Freq_Shift       : 4;  // 13-10 Noise Frequency Shift   (0..0Fh = Low .. High Frequency)
            uint16_t Mute_SPU               : 1;  // 14    Mute SPU                (0=Mute, 1=Unmute)  (Don't care for CD Audio)
            uint16_t SPU_Enable             : 1;  // 15    SPU Enable              (0=Off, 1=On)       (Don't care for CD Audio)
        };

        uint16_t _reg;

        Spucnt() = default;
        Spucnt(uint32_t reg) : _reg(reg) {}
    };

    struct VolumeSweep {
        uint16_t reg = 0;
        int16_t current = 0;
        int32_t cycleCounter = 0;

        void write(uint16_t value) {
            reg = value;

            if ((value & 0x8000) == 0) {
                int32_t level = value & 0x7FFF;

                if (level & 0x4000)
                    level |= ~0x7FFF;

                current = static_cast<int16_t>(std::clamp<int32_t>(level * 2, -32768, 32767));
                cycleCounter = 0;
            }
        }

        void step() {
            if ((reg & 0x8000) == 0)
                return;

            bool isExponential = (reg >> 14) & 1;
            bool isDecrease = (reg >> 13) & 1;
            int32_t shiftVal = (reg >> 2) & 0x1F;
            uint8_t stepIdx = reg & 3;

            if (cycleCounter > 0) {
                cycleCounter--;

                return;
            }

            static const int inc[] = { 7, 6, 5, 4 };
            static const int dec[] = { -8, -7, -6, -5 };

            int32_t stepVal = isDecrease ? dec[stepIdx] : inc[stepIdx];
            int32_t cycle = 1 << std::max(0, shiftVal - 11);
            int32_t sweepStep = stepVal << std::max(0, 11 - shiftVal);

            if (isExponential && isDecrease)
                sweepStep = (sweepStep * current) >> 15;

            if (isExponential && !isDecrease && current > 0x6000)
                cycle *= 4;

            cycleCounter = cycle - 1;

            current = static_cast<int16_t>(std::clamp<int32_t>(current + sweepStep, 0, 0x7FFF));
        }
    };

    // SPU Volume and ADSR Generator
    struct ADSR {
        enum State: int {
            Attack = 0, 
            Decay = 1, 
            Sustain = 2, 
            Release = 3, 
            Off = 4
        };

        // 1F801C08h+N*10h - Voice 0..23 Attack/Decay/Sustain/Release (ADSR) (32bit)
        uint32_t adsr = 0;

        // 1F801C00h+N*10h - Voice 0..23 Volume Left
        VolumeSweep volLeft = {};

        // 1F801C02h+N*10h - Voice 0..23 Volume Right
        VolumeSweep volRight = {};

        // 1F801C0Ch+N*10h - Voice 0..23 Current ADSR volume (R/W)
        int16_t currentVolume = 0;

        int32_t cycleCounter = 0;

        State state = State::Off;

        void step() {
            if (state == Off) return;

            int32_t stepVal = 0;
            int32_t shiftVal = 0;
            bool isExponential = false;
            bool isDecrease = false;
            int32_t targetLevel = currentVolume;

            switch (state) {
                case Release: {
                    stepVal = -8;
                    shiftVal = (adsr >> 16) & 0x1F;
                    isExponential = ((adsr >> 21) & 1) != 0;
                    isDecrease = true;
                    targetLevel = 0;
                    break;
                }
                case Attack: {
                    //stepVal = ((adsr >> 8) & 3) ^ 0; // attack step 7..4
                    stepVal = 7 - ((adsr >> 8) & 3);
                    shiftVal = (adsr >> 10) & 0x1F;
                    isExponential = ((adsr >> 15) & 1) != 0;
                    isDecrease = false;
                    targetLevel = 0x7FFF;
                    break;
                }
                case Decay: {
                    stepVal = -8;
                    shiftVal = (adsr >> 4) & 0xF;
                    isExponential = true;
                    isDecrease = true;
                    targetLevel = std::min<int32_t>(((adsr & 0xF) + 1) * 0x800, 0x7FFF);

                    break;
                }
                case Sustain: {
                    uint8_t stepIdx = (adsr >> 22) & 3;
                    isExponential = ((adsr >> 31) & 1) != 0;
                    isDecrease = ((adsr >> 30) & 1) != 0;
                    shiftVal = (adsr >> 24) & 0x1F;

                    if (isDecrease) {
                        static const int s[] = { -8, -7, -6, -5 };
                        stepVal = s[stepIdx];
                        targetLevel = 0;
                    } else {
                        static const int s[] = { 7, 6, 5, 4 };
                        stepVal = s[stepIdx];
                        targetLevel = 0x7FFF;
                    }

                    break;
                }
            }

            if (cycleCounter > 0) {
                cycleCounter--;

                return;
            }

            int32_t cycle = 1 << std::max(0, shiftVal - 11);
            int32_t adsrStep = stepVal << std::max(0, 11 - shiftVal);

            if (isExponential && isDecrease)
                adsrStep = (adsrStep * currentVolume) >> 15;

            if (isExponential && !isDecrease && currentVolume > 0x6000)
                cycle *= 4;
            
            cycleCounter = cycle - 1;

            currentVolume = std::clamp(static_cast<int32_t>(currentVolume) + adsrStep, 0, 0x7FFF);

            switch (state) {
                case Attack:
                    if (currentVolume >= targetLevel) { state = Decay; cycleCounter = 0; }
                    break;
                case Decay:
                    if (currentVolume <= targetLevel) { state = Sustain; cycleCounter = 0; }
                    break;
                case Sustain:
                    // Doesn't change until keyoff
                    break;
                case Release:
                    if (currentVolume <= 0) { currentVolume = 0; state = Off; cycleCounter = 0; }
                    break;
                default: break;
            }
        }
    };

    // SPU ADPCM Pitch
    struct ADPCM {
        // ADPCM Sample Rate
        uint32_t vxPitch = 0;

        void step() {

        }
    };

    class SPU {
    struct Voice {
        uint32_t pitchCounter = 0;

        // Start Address (This gets copied to "current address" upon Key On)
        uint32_t startAddress = 0;

        uint32_t repeatAddress = 0;
        uint32_t currentAddress = 0;

        // Pitch Modulation Enable Flags
        uint8_t PMON = 0;

        bool muted = false;

        ADPCM adpcm = {};
        ADSR adsr = {};

        const int16_t kGaussianTable[512] = {
            // 000h..07Fh
            -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
            -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
             0x000,  0x000,  0x000,  0x000,  0x000,  0x000,  0x000,  0x001,
             0x001,  0x001,  0x001,  0x002,  0x002,  0x002,  0x003,  0x003,
             0x003,  0x004,  0x004,  0x005,  0x005,  0x006,  0x007,  0x007,
             0x008,  0x009,  0x009,  0x00A,  0x00B,  0x00C,  0x00D,  0x00E,
             0x00F,  0x010,  0x011,  0x012,  0x013,  0x015,  0x016,  0x018,
             0x019,  0x01B,  0x01C,  0x01E,  0x020,  0x021,  0x023,  0x025,
             0x027,  0x029,  0x02C,  0x02E,  0x030,  0x033,  0x035,  0x038,
             0x03A,  0x03D,  0x040,  0x043,  0x046,  0x049,  0x04D,  0x050,
             0x054,  0x057,  0x05B,  0x05F,  0x063,  0x067,  0x06B,  0x06F,
             0x074,  0x078,  0x07D,  0x082,  0x087,  0x08C,  0x091,  0x096,
             0x09C,  0x0A1,  0x0A7,  0x0AD,  0x0B3,  0x0BA,  0x0C0,  0x0C7,
             0x0CD,  0x0D4,  0x0DB,  0x0E3,  0x0EA,  0x0F2,  0x0FA,  0x0101,
             0x10A,  0x112,  0x11B,  0x123,  0x12C,  0x135,  0x13F,  0x148,
             0x152,  0x15C,  0x166,  0x171,  0x17B,  0x186,  0x191,  0x19C,

            // 080h..0FFh
            0x1A8,  0x1B4,  0x1C0,  0x1CC,  0x1D9,  0x1E5,  0x1F2,  0x200,
            0x20D,  0x21B,  0x229,  0x237,  0x246,  0x255,  0x264,  0x273,
            0x283,  0x293,  0x2A3,  0x2B4,  0x2C4,  0x2D6,  0x2E7,  0x2F9,
            0x30B,  0x31D,  0x330,  0x343,  0x356,  0x36A,  0x37E,  0x392,
            0x3A7,  0x3BC,  0x3D1,  0x3E7,  0x3FC,  0x413,  0x42A,  0x441,
            0x458,  0x470,  0x488,  0x4A0,  0x4B9,  0x4D2,  0x4EC,  0x506,
            0x520,  0x53B,  0x556,  0x572,  0x58E,  0x5AA,  0x5C7,  0x5E4,
            0x601,  0x61F,  0x63E,  0x65C,  0x67C,  0x69B,  0x6BB,  0x6DC,
            0x6FD,  0x71E,  0x740,  0x762,  0x784,  0x7A7,  0x7CB,  0x7EF,
            0x813,  0x838,  0x85D,  0x883,  0x8A9,  0x8D0,  0x8F7,  0x91E,
            0x946,  0x96F,  0x998,  0x9C1,  0x9EB,  0xA16,  0xA40,  0xA6C,
            0xA98,  0xAC4,  0xAF1,  0xB1E,  0xB4C,  0xB7A,  0xBA9,  0xBD8,
            0xC07,  0xC38,  0xC68,  0xC99,  0xCCB,  0xCFD,  0xD30,  0xD63,
            0xD97,  0xDCB,  0xE00,  0xE35,  0xE6B,  0xEA1,  0xED7,  0xF0F,
            0xF46,  0xF7F,  0xFB7,  0xFF1,  0x102A, 0x1065, 0x109F, 0x10DB,
            0x1116, 0x1153, 0x118F, 0x11CD, 0x120B, 0x1249, 0x1288, 0x12C7,

            // 100h..17Fh
            0x1307, 0x1347, 0x1388, 0x13C9, 0x140B, 0x144D, 0x1490, 0x14D4,
            0x1517, 0x155C, 0x15A0, 0x15E6, 0x162C, 0x1672, 0x16B9, 0x1700,
            0x1747, 0x1790, 0x17D8, 0x1821, 0x186B, 0x18B5, 0x1900, 0x194B,
            0x1996, 0x19E2, 0x1A2E, 0x1A7B, 0x1AC8, 0x1B16, 0x1B64, 0x1BB3,
            0x1C02, 0x1C51, 0x1CA1, 0x1CF1, 0x1D42, 0x1D93, 0x1DE5, 0x1E37,
            0x1E89, 0x1EDC, 0x1F2F, 0x1F82, 0x1FD6, 0x202A, 0x207F, 0x20D4,
            0x2129, 0x217F, 0x21D5, 0x222C, 0x2282, 0x22DA, 0x2331, 0x2389,
            0x23E1, 0x2439, 0x2492, 0x24EB, 0x2545, 0x259E, 0x25F8, 0x2653,
            0x26AD, 0x2708, 0x2763, 0x27BE, 0x281A, 0x2876, 0x28D2, 0x292E,
            0x298B, 0x29E7, 0x2A44, 0x2AA1, 0x2AFF, 0x2B5C, 0x2BBA, 0x2C18,
            0x2C76, 0x2CD4, 0x2D33, 0x2D91, 0x2DF0, 0x2E4F, 0x2EAE, 0x2F0D,
            0x2F6C, 0x2FCC, 0x302B, 0x308B, 0x30EA, 0x314A, 0x31AA, 0x3209,
            0x3269, 0x32C9, 0x3329, 0x3389, 0x33E9, 0x3449, 0x34A9, 0x3509,
            0x3569, 0x35C9, 0x3629, 0x3689, 0x36E8, 0x3748, 0x37A8, 0x3807,
            0x3867, 0x38C6, 0x3926, 0x3985, 0x39E4, 0x3A43, 0x3AA2, 0x3B00,
            0x3B5F, 0x3BBD, 0x3C1B, 0x3C79, 0x3CD7, 0x3D35, 0x3D92, 0x3DEF,

            // 180h..1FFh
            0x3E4C, 0x3EA9, 0x3F05, 0x3F62, 0x3FBD, 0x4019, 0x4074, 0x40D0,
            0x412A, 0x4185, 0x41DF, 0x4239, 0x4292, 0x42EB, 0x4344, 0x439C,
            0x43F4, 0x444C, 0x44A3, 0x44FA, 0x4550, 0x45A6, 0x45FC, 0x4651,
            0x46A6, 0x46FA, 0x474E, 0x47A1, 0x47F4, 0x4846, 0x4898, 0x48E9,
            0x493A, 0x498A, 0x49D9, 0x4A29, 0x4A77, 0x4AC5, 0x4B13, 0x4B5F,
            0x4BAC, 0x4BF7, 0x4C42, 0x4C8D, 0x4CD7, 0x4D20, 0x4D68, 0x4DB0,
            0x4DF7, 0x4E3E, 0x4E84, 0x4EC9, 0x4F0E, 0x4F52, 0x4F95, 0x4FD7,
            0x5019, 0x505A, 0x509A, 0x50DA, 0x5118, 0x5156, 0x5194, 0x51D0,
            0x520C, 0x5247, 0x5281, 0x52BA, 0x52F3, 0x532A, 0x5361, 0x5397,
            0x53CC, 0x5401, 0x5434, 0x5467, 0x5499, 0x54CA, 0x54FA, 0x5529,
            0x5558, 0x5585, 0x55B2, 0x55DE, 0x5609, 0x5632, 0x565B, 0x5684,
            0x56AB, 0x56D1, 0x56F6, 0x571B, 0x573E, 0x5761, 0x5782, 0x57A3,
            0x57C3, 0x57E2, 0x57FF, 0x581C, 0x5838, 0x5853, 0x586D, 0x5886,
            0x589E, 0x58B5, 0x58CB, 0x58E0, 0x58F4, 0x5907, 0x5919, 0x592A,
            0x593A, 0x5949, 0x5958, 0x5965, 0x5971, 0x597C, 0x5986, 0x598F,
            0x5997, 0x599E, 0x59A4, 0x59A9, 0x59AD, 0x59B0, 0x59B2, 0x59B3
        };

         // A reference to the previous samples,
        // used for Gaussian
        int16_t oldSample;
        int16_t olderSample;
        int16_t currentSample = 0;

        int16_t decodedSamples[28];  // Must be 28 samples per ADPCM block
        int16_t sampleHistory[4] = {0, 0, 0, 0}; // [0]=new, [1]=old, [2]=older, [3]=oldest
        uint8_t currentSampleIndex = 0;

        // TODO: REMOVE
        int16_t currentLeft;
        int16_t currentRight;

        /**
         *
         * @param voiceIndex - Current voice index
         * @param vxOut - Output of previous voice
         * @param ram - A reference to the soundRAM
         */
        void step(uint8_t voiceIndex, int16_t vxOut, SPU &spu) {
            adpcm.step();
            adsr.step();

            if (adsr.state == ADSR::Off) {
                currentSample = 0;
                currentLeft = 0;
                currentRight = 0;

                return;
            }

            // ;range +0000h..+FFFFh (0...705.6 kHz)
            int32_t step = adpcm.vxPitch;

            // If pitch modulation is enabled
            if (PMON == 1 && voiceIndex > 0) {     // ;pitch modulation enable
                int16_t factor = vxOut;            // ;range -8000h..+7FFFh (prev voice amplitude)
                factor = factor + 0x8000;          // ;range +0000h..+FFFFh (factor = 0.00 .. 1.99)
                step = static_cast<int16_t>(step); // ;hardware glitch on VxPitch>7FFFh, make sign
                step = (step * factor) >> 15;      // ;range 0..1FFFFh (glitchy if VxPitch>7FFFh)
                step &= 0xffff;                    // ;hardware glitch on VxPitch>7FFFh, kill sign
            }

            if (step > 0x3FFF)                        // ;range +0000h..+3FFFh (0.. 176.4kHz)
                step = 0x4000;

            pitchCounter += step;

            while (pitchCounter >= 0x1000) {
                pitchCounter -= 0x1000;
                currentSampleIndex++;

                if (currentSampleIndex >= 28) {
                    currentSampleIndex = 0;
                    advanceToNextBlock(voiceIndex, spu);
                }

                sampleHistory[3] = sampleHistory[2];
                sampleHistory[2] = sampleHistory[1];
                sampleHistory[1] = sampleHistory[0];
                sampleHistory[0] = decodedSamples[currentSampleIndex];
            }

            // Counter.Bit12 and up indicates the current sample (within a ADPCM block).
            // Counter.Bit4..11 are used as 8bit gaussian interpolation index.

            uint32_t i = (pitchCounter >> 4) & 0xFF;

            int32_t interpolated = 0;

            if ((spu.NON >> voiceIndex) & 1) {
                interpolated = spu.noiseLevel;
            } else {
                interpolated += (kGaussianTable[0x0FF - i] * sampleHistory[3]) >> 15;
                interpolated += (kGaussianTable[0x1FF - i] * sampleHistory[2]) >> 15;
                interpolated += (kGaussianTable[0x100 + i] * sampleHistory[1]) >> 15;
                interpolated += (kGaussianTable[0x000 + i] * sampleHistory[0]) >> 15;
            }

            adsr.volLeft.step();
            adsr.volRight.step();

            int32_t gatedSample = (interpolated * adsr.currentVolume) >> 15;

            currentSample = static_cast<int16_t>(std::clamp<int32_t>(gatedSample, -32768, 32767));

            currentLeft  = static_cast<int16_t>(std::clamp<int32_t>((gatedSample * adsr.volLeft.current)  >> 15, -32768, 32767));
            currentRight = static_cast<int16_t>(std::clamp<int32_t>((gatedSample * adsr.volRight.current) >> 15, -32768, 32767));
        }

        void decodeEntireBlock(SPU &spu) {
            uint16_t header = spu.readFromRAM16(currentAddress);

            uint8_t shift = header & 0x0F;
            shift = shift > 12 ? 9 : shift;

            uint8_t filterIdx = std::min(4, (header >> 4) & 0x07);
            int16_t prev1 = oldSample;
            int16_t prev2 = olderSample;

            for (int i = 0; i < 28; i++) {
                uint16_t sampleWord = spu.readFromRAM16(currentAddress + 1 + i / 4);
                int16_t nibble = (sampleWord >> (4 * (i % 4))) & 0x0F;

                if (nibble & 0x8) {
                    nibble |= 0xFFF0;
                }

                int32_t shiftedSample = nibble << (12 - shift);

                int32_t filteredSample = shiftedSample;
                switch (filterIdx) {
                    case 0: {
                        filteredSample = shiftedSample;
                        break;
                    }
                    case 1: {
                        filteredSample = shiftedSample + (60 * prev1 + 32) / 64;
                        break;
                    }
                    case 2: {
                        filteredSample = shiftedSample + (115 * prev1 - 52 * prev2 + 32) / 64;
                        break;
                    }
                    case 3: {
                        filteredSample = shiftedSample + (98 * prev1 - 55 * prev2 + 32) / 64;
                        break;
                    }
                    case 4: {
                        filteredSample = shiftedSample + (122 * prev1 - 60 * prev2 + 32) / 64;
                        break;
                    }
                }

                int16_t clamppedSample = static_cast<int16_t>(std::clamp<int32_t>(filteredSample, -32768, 32767));
                decodedSamples[i] = clamppedSample;

                prev2 = prev1;
                prev1 = clamppedSample;
            }

            olderSample = prev2;
            oldSample = prev1;
        }

        void advanceToNextBlock(uint8_t voiceIndex, SPU &spu) {
            decodeEntireBlock(spu);

            uint8_t flags = (spu.readFromRAM16(currentAddress) >> 8) & 0xFF;

            // Loop start
            if ((flags & 0x04) != 0) {
                repeatAddress = currentAddress;
            }

            if ((flags & 0x01) != 0) { // Loop End
                spu.ENDX |= (1u << voiceIndex);
                currentAddress = repeatAddress;

                if ((flags & 0x02) == 0) {
                    adsr.state = ADSR::Release;
                    adsr.currentVolume = 0;
                }
            } else {
                currentAddress += 8;
            }
        }

        int cycles;

        void triggerKeyOn(uint8_t voiceIndex, int cycles, SPU &spu) {
            currentAddress = startAddress * 4;
            repeatAddress = currentAddress;

            adsr.currentVolume = 0;
            adsr.state = ADSR::Attack;
            adsr.cycleCounter = 0;

            pitchCounter = 0;
            currentSampleIndex = 0;

            oldSample = 0;
            olderSample = 0;
            currentLeft = 0;
            currentRight = 0;
            currentSample = 0;

            std::fill(std::begin(sampleHistory), std::end(sampleHistory), 0);

            spu.ENDX &= ~(1u << voiceIndex);

            advanceToNextBlock(voiceIndex, spu);

            sampleHistory[0] = decodedSamples[0];

            this->cycles = cycles;
        }

        void triggerKeyOff(int cycles) {
            /*if (cycles != 0 && cycles - this->cycles < 384) {
                return;
            }*/

            // and switches from Sustain to Release when the software sets the Key OFF flag.
            adsr.state = ADSR::Release;
            adsr.cycleCounter = 0;

            //currentLeft = 0;
            //currentRight = 0;
        }
    };


        public:
            SPU();

            void step(uint32_t cycles);
            void stepTransfer();

            uint32_t load(uint32_t addr);
            void store(uint32_t addr, uint32_t val);

        private:
            uint32_t handleVoiceLoad(uint32_t addr);
            void handleVoiceStore(uint32_t addr, uint32_t val);

            uint32_t handleVolumeLoad(uint32_t addr);
            void handleVolumeStore(uint32_t addr, uint32_t val);

            uint32_t handleFlagsLoad(uint32_t addr);
            void handleFlagsStore(uint32_t addr, uint32_t val);

            uint32_t handleControlLoad(uint32_t addr);
            void handleControlStore(uint32_t addr, uint32_t val);

            uint32_t handleVoiceInternalLoad(uint32_t addr);
            void handleVoiceInternalStore(uint32_t addr, uint32_t val);

            void pushSamples(int16_t* samples, int sampleCount);

        public:
            void writeToRAM8(uint32_t addr, uint8_t val);
            uint8_t readFromRAM8(uint32_t addr);

            void writeToRAM16(uint32_t addr, uint16_t val);
            uint16_t readFromRAM16(uint32_t addr);

        public: // TODO: Revert to private
            // SPU Noise Generator

            // Reverb Registers
            uint16_t vLOUT = 0;
            uint16_t vROUT = 0;

            // Echo On
            uint32_t EON = 0;

            // Pitch Modulation Enabled Flags
            uint32_t PMON = 0;

            // Noise mode enable
            uint32_t NON = 0;

            // 1F801D80h - Main volume left
            uint16_t mainValLeft = 0;

            // 1F801D82h - Main volume right
            uint16_t mainValRight = 0;
            uint64_t queuedBufferCount = 0;
            uint64_t bufferIndex = 0;

            // User-controlled output settings (not part of the emulated hardware)
            bool  audioEnabled = true;
            float masterVolume = 1.0f;

            uint32_t KON = 0;
            uint32_t KOFF = 0;
            uint32_t ENDX = 0;

            int32_t noiseTimer = 0;
            int16_t noiseLevel = 0;

            // 1F801DB0h - CD Audio Input Volume (for normal CD-DA, and compressed XA-ADPCM)
            int16_t cdInputVolLeft = 0;
            int16_t cdInputVolRight = 0;

            // 1F801DB4h - External Audio Input Volume
            uint16_t exVolLeft = 0;
            uint16_t exVolRight = 0;

            // 1F801DACh - Sound RAM Data Transfer Control (should be 0004h)
            uint16_t transferControl = 0;
            uint16_t transferCache = 0;
            uint32_t transferIndex = 0;

        private:
            uint16_t irqAddress = 0; // (IRQ9)
            uint16_t transferAddress = 0;
            uint32_t currentAddress = 0;

            static const uint8_t VOICE_COUNT = 24;

        public:
            Status status = {0};
            Spucnt spunct = {0};

            // Registers
            //Flags flags[VOICE_COUNT];
            //ADPCM adpcm[VOICE_COUNT];
            //ADSR adsr[VOICE_COUNT];

            Voice voices[VOICE_COUNT] = {};

            /**
             * SPU Memory layout (512Kbyte RAM)
             *   00000h-003FFh  CD Audio left  (1Kbyte) ;\CD Audio before Volume processing
             *   00400h-007FFh  CD Audio right (1Kbyte) ;/signed 16bit samples at 44.1kHz
             *   00800h-00BFFh  Voice 1 mono   (1Kbyte) ;\Voice 1 and 3 after ADSR processing
             *   00C00h-00FFFh  Voice 3 mono   (1Kbyte) ;/signed 16bit samples at 44.1kHz
             *   01000h-xxxxxh  ADPCM Samples  (first 16bytes usually contain a Sine wave)
             *   xxxxxh-7FFFFh  Reverb work area
             */
            std::vector<uint16_t> soundRAM = std::vector<uint16_t>(256 * 1024, 0);

            Fifo buffer;

        private:
            SDL_AudioDeviceID device;
    };
}
