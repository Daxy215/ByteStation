#include "Gpu.h"

#include <algorithm>
#include <cassert>
#include <deque>

#include "Rendering/Renderer.h"
#include "VRAM.h"

#include <ios>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "../Memory/IRQ.h"
// #include "gte.h"

namespace {
    unsigned long long gpuLogSequence = 0;

    unsigned long long nextGpuLogSequence() { return gpuLogSequence++; }
} // namespace

static const Emulator::Gpu::BitField polygonFields[] = {
    {"rgb",               0, 24},
    {"isRawTexture",     24, 1},
    {"isSemiTransparent",25, 1},
    {"isTextured",       26, 1},
    {"isFourVerts",      27, 1},
    {"isGouraud",        28, 1},
};

static const Emulator::Gpu::BitField lineFields[] = {
    {"rgb",               0, 24},
    {"isSemiTransparent",25, 1},
    {"isPolyline",       27, 1},
    {"gouraud",          28, 1},
};

static const Emulator::Gpu::BitField rectangleFields[] = {
    {"rgb",               0, 24},
    {"isRawTexture",      24, 1},
    {"isSemiTransparent", 25, 1},
    {"isTextured",        26, 1},
    /**
     * 0 (00)      variable size
     * 1 (01)      single pixel (1x1)
     * 2 (10)      8x8 sprite
     * 3 (11)      16x16 sprite
     */
    {"rectangleSize",     27, 2},
};

Emulator::Gpu::Gpu(bool enableRendering)
    : pageBaseX(0),
      pageBaseY(0),
      semiTransparency(0),
      textureDepth(TextureDepth::T4Bit),
      dithering(false),
      drawToDisplay(false),
      forceSetMaskBit(false),
      preserveMaskedPixels(false),
      field(Field::Top),
      textureDisable(false),
      hres(HorizontalRes::fromFields(320, 240)),
      vres(VerticalRes::Y240Lines),
      vmode(VMode::Ntsc),
      displayDepth(DisplayDepth::D15Bits),
      interlaced(false),
      displayEnabled(false),
      interrupt(false),
      dmaDirection(DmaDirection::Off),
      displayHorizFlip(false) {
        if (enableRendering) {
            renderer = new Renderer(*this);
            vram = new VRAM(*this);
        }

        //renderer->init();
        reset();
}

bool Emulator::Gpu::step(uint32_t cpuCycles) {
    //lastDotTicks = 0;

    uint64_t num = vmode == VMode::Pal ? PAL_CRTC_NUM : NTSC_CRTC_NUM;
    uint64_t den = vmode == VMode::Pal ? PAL_CRTC_DEN : NTSC_CRTC_DEN;

    uint64_t n     = uint64_t(cpuCycles) * num + _gpuFrac;
    uint32_t ticks = uint32_t(n / den);
    _gpuFrac       = n % den;

    lastGpuCycles = ticks;

    return stepCRTC(ticks);
}

bool Emulator::Gpu::stepCRTC(uint32_t ticks) {
    bool enteredVBlank = false;

    const uint32_t lineTotal = htotal();
    const uint32_t frameTotal = vtotal();

    while (ticks > 0) {
        const uint32_t divider = dotClockDivider();
        const uint32_t run = std::min(ticks, ticksUntilNextCRTCEvent());

        dotFrac += run;
        //lastDotTicks += dotFrac / divider;
        dotFrac %= divider;

        _hpos += run;
        ticks -= run;

        if (_hpos >= lineTotal) {
            _hpos -= lineTotal;
            _scanLine++;

            if (_scanLine >= frameTotal) {
                _scanLine = 0;
                frames++;
            }
        }

        updateDotClock();

        const bool oldHBlank = isInHBlank;
        const bool oldVBlank = isInVBlank;

        isInHBlank = calcHBlank();
        isInVBlank = calcVBlank();

        if (!oldHBlank && isInHBlank)
            onHBlankStart();

        if (oldHBlank && !isInHBlank)
            onHBlankEnd();

        if (!oldVBlank && isInVBlank) {
            displayField = !displayField;

            onVBlankStart();

            enteredVBlank = true;
        }

        if (oldVBlank && !isInVBlank)
            onVBlankEnd();

        updateOddLineBit();
    }

    return enteredVBlank;
}

uint32_t Emulator::Gpu::ticksUntilNextCRTCEvent() const {
    uint32_t total = htotal();

    uint32_t hblankS = hblankStart();
    uint32_t hblankE = hblankEnd();

    uint32_t next;// = total - _hpos;

    /*if (_hpos < hblankE)
        next = std::min(next, hblankE - _hpos);

    if (_hpos < hblankS)
        next = std::min(next, hblankS - _hpos);*/

    if (_hpos >= hblankS)
        next = total - _hpos + hblankE;
    else if (_hpos < hblankE)
        next = hblankE - _hpos;
    else
        next = hblankS - _hpos;

    next = std::min(next, total - _hpos);

    uint32_t vstart = std::min<uint32_t>(displayLineStart, vtotal());
    uint32_t vend   = std::min<uint32_t>(displayLineEnd, vtotal());

    if (_hpos == 0) {
        if (_scanLine < vstart)
            next = std::min(next, 1u);

        if (_scanLine < vend)
            next = std::min(next, 1u);
    }

    return std::max(next, 1u);
}

uint32_t Emulator::Gpu::cpuCyclesUntilNextCRTCEvent() const {
    uint64_t num = vmode == VMode::Pal ? PAL_CRTC_NUM : NTSC_CRTC_NUM;
    uint64_t den = vmode == VMode::Pal ? PAL_CRTC_DEN : NTSC_CRTC_DEN;

    uint64_t needed = uint64_t(ticksUntilNextCRTCEvent()) * den;
    uint64_t remaining = needed > _gpuFrac ? needed - _gpuFrac : 0;

    return static_cast<uint32_t>(std::max<uint64_t>((remaining + num - 1) / num, 1));
}

/*bool Emulator::Gpu::step(uint32_t cycles) {
    /**
     * Dots per scanline are, depending on horizontal resolution, and on PAL/NTSC:
     * 256pix/PAL: 3406/10 = 340.6 dots      256pix/NTSC: 3413/10 = 341.3 dots
     * 320pix/PAL: 3406/8  = 425.75 dots     320pix/NTSC: 3413/8  = 426.625 dots
     * 512pix/PAL: 3406/5  = 681.2 dots      512pix/NTSC: 3413/5  = 682.6 dots
     * 640pix/PAL: 3406/4  = 851.5 dots      640pix/NTSC: 3413/4  = 853.25 dots
     * 368pix/PAL: 3406/7  = 486.5714 dots   368pix/NTSC: 3413/7  = 487.5714 dots
     #1#

    /**
      * The PSone/PAL video clock is the cpu clock multiplied by 11/7.
      * CPU Clock   =  33.868800MHz (44100Hz*300h)
      * Video Clock =  53.222400MHz (44100Hz*300h*11/7) or 53.690000MHz in Ntsc mode?
      *
      * Frame Rates
      * PAL:  53.222400MHz/314/3406 = ca. 49.76 Hz (ie. almost 50Hz)
      * NTSC: 53.222400MHz/263/3413 = ca. 59.29 Hz (ie. almost 60Hz)
      #1#
    static constexpr uint64_t CPU_CLOCK = 33868800;
    static constexpr uint64_t GPU_CLOCK_NTSC = 53693175;
    static constexpr uint64_t GPU_CLOCK_PAL = 53203425;

    static constexpr uint32_t FP_SHIFT = 32;
    static constexpr uint64_t FP_ONE = 1ULL << FP_SHIFT;

    static constexpr uint64_t HTIMING_NTSC = static_cast<uint64_t>((static_cast<u128>(GPU_CLOCK_NTSC) * 1001 * 2 *
FP_ONE + ((60000ULL * 525) / 2)) / (60000ULL * 525)); static constexpr uint64_t HTIMING_PAL =
static_cast<uint64_t>((static_cast<u128>(GPU_CLOCK_PAL) * FP_ONE + (15625 / 2)) / 15625); uint64_t gpuClock = vmode ==
VMode::Pal ? GPU_CLOCK_PAL : GPU_CLOCK_NTSC;

    /**
     * Horizontal Timings
     * PAL:  3406 video cycles per scanline (or 3406.1 or so?)
     * NTSC: 3413 video cycles per scanline (or 3413.6 or so?)
     #1#
    uint64_t htiming = vmode == VMode::Pal ? HTIMING_PAL : HTIMING_NTSC;

    /**
    * Vertical Timings
    * PAL:  314 scanlines per frame (13Ah)
    * NTSC: 263 scanlines per frame (107h)
    #1#
    uint32_t vtiming = ((vmode == VMode::Pal) ? 314 : 263);

    u128 n = static_cast<u128>(cycles) * gpuClock * FP_ONE + _gpuFrac;
    uint64_t advanced = static_cast<uint64_t>(n / CPU_CLOCK);

    _cycles += advanced;
    _gpuFrac = static_cast<uint64_t>(n % CPU_CLOCK);

    _gpuTimerFrac += advanced;
    lastGpuCycles = static_cast<uint32_t>(_gpuTimerFrac >> FP_SHIFT);
    _gpuTimerFrac &= FP_ONE - 1;

    while (_cycles >= htiming) {
        _cycles -= htiming;
        _scanLine++;

        // Ig this is wrong?
        if (_scanLine >= vtiming) {
            _scanLine = 0;
            frames++;
        }
    }

    dot = static_cast<uint32_t>(_cycles >> FP_SHIFT);

    //uint32_t hblankStart = displayHorizEnd;
    //uint32_t hblankEnd = displayHorizStart;
    //isInHBlank = (dot >= hblankStart) && (dot < hblankEnd);

    // I think Mednafen does this?
    if (displayHorizStart <= displayHorizEnd)
        isInHBlank = dot < displayHorizStart || dot >= displayHorizEnd;
    else
        isInHBlank = dot >= displayHorizEnd && dot < displayHorizStart;

    bool oldVBlank = isInVBlank;

    //isInVBlank = _scanLine < displayLineStart || _scanLine >= displayLineEnd;
    if (displayLineStart <= displayLineEnd)
        isInVBlank = _scanLine < displayLineStart || _scanLine >= displayLineEnd;
    else
        isInVBlank = _scanLine >= displayLineEnd && _scanLine < displayLineStart;

    if (!isInVBlank) {
        // Bit31 In 480-lines mode, bit31 changes per frame.
        // And in 240-lines mode, the bit changes per scanline.
        // The bit is always zero during Vblank (vertical retrace and upper/lower screen border).

        if (interlaced && vres == VerticalRes::Y480Lines)
            isOddLine = (frames & 1);
        else
            isOddLine = (_scanLine & 1);
    } else {
        isOddLine = false;
    }

    bool enteredVBlank = !oldVBlank && isInVBlank;

    return enteredVBlank;
}*/

uint32_t Emulator::Gpu::status() {
    // https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#1f801814h-gpustat-gpu-status-register-r

    uint32_t status = 0;

    // Bits 0-3: Texture page X base (N * 64)
    status |= (static_cast<uint32_t>(pageBaseX) & 0x0F) << 0;

    // Bit 4: Texture page Y base 1 (N * 256)
    status |= (static_cast<uint32_t>(pageBaseY) & 0x01) << 4;

    // Bits 5-6: Semi-transparency mode
    status |= (static_cast<uint32_t>(semiTransparency) & 0x03) << 5; // Mask to 2 bits

    // Bits 7-8: Texture page color depth
    status |= (static_cast<uint32_t>(textureDepth) & 0x03) << 7; // Mask to 2 bits

    // Bit 9: Dither 24bit to 15bit (0=Off, 1=Enabled)
    status |= static_cast<uint32_t>(dithering ? 1 : 0) << 9;

    // Bit 10: Interlace Drawing (0=All Lines, 1=Inactive Lines)
    status |= static_cast<uint32_t>(drawToDisplay ? 1 : 0) << 10;

    // Bit 11: Set Mask-bit when drawing (0=No, 1=Yes)
    status |= static_cast<uint32_t>(forceSetMaskBit ? 1 : 0) << 11;

    // Bit 12: Don't draw to masked areas (0=Always, 1=Not to masked areas)
    status |= static_cast<uint32_t>(preserveMaskedPixels ? 1 : 0) << 12;

    // Bit 13: Interlace Field (0 = Top, 1 = Bottom)
    // (or, always 1 when GP1(08h).5=0)
    // status |= (interlaced ? 1 : static_cast<uint8_t>(field)) << 13;
    // status |= (static_cast<uint8_t>(field)) << 13;
    status |= static_cast<uint32_t>(interlaced ? (frames & 1u) : 1u) << 13;

    // Bit 14: Flip screen horizontally (0=Off, 1=On, v1 only)
    status |= static_cast<uint32_t>(displayHorizFlip ? 1 : 0) << 14;

    // GPUSTAT.15: bit1 of texpage Y base on 2 MB VRAM GPUs.
    status |= static_cast<uint32_t>(textureDisable ? 1 : 0) << 15;

    status |= hres.intoStatus();

    status |= (static_cast<uint32_t>(vres) & 0x01) << 19;

    // Bit 20: Video mode (0=NTSC, 1=PAL)
    status |= (static_cast<uint32_t>(vmode) & 0x01) << 20;

    // Bit 21: Display area color depth (0=15bit, 1=24bit)
    status |= (static_cast<uint32_t>(displayDepth) & 0x01) << 21;

    // Bit 22: Vertical Interlace (0=Off, 1=On)
    status |= static_cast<uint32_t>(interlaced ? 1 : 0) << 22;

    // Bit 23: Display enable (0=Enabled, 1=Disabled)
    status |= static_cast<uint32_t>(displayEnabled ? 0 : 1) << 23;

    // Bit 24: Interrupt request (0=Off, 1=IRQ)
    status |= static_cast<uint32_t>(interrupt ? 1 : 0) << 24;

    const uint32_t readyForCommand = readyToReceiveCommandWord() ? 1u : 0u;
    const uint32_t readyToReadVram = readyToSendVramToCpu() ? 1u : 0u;
    const uint32_t readyForDmaBlock = readyToReceiveDmaBlock() ? 1u : 0u;

    // Bit 26: Ready to receive command word (0=No, 1=Ready)
    // Cleared while the GPU is receiving parameters/image data or serving GPUREAD image data.
    status |= readyForCommand << 26;

    // Bit 27: Ready to send VRAM to CPU (0=No, 1=Ready)
    /**
     * Bit27: Gets set after sending GP0(C0h) and its parameters,
     * and stays set until all data words are received; used as DMA request in DMA Mode 3.
     */
    status |= readyToReadVram << 27;
    
    // Bit 28: Ready to receive DMA block (0=No, 1=Ready)
    /**
     * Bit28: Normally, this bit gets cleared when the command execution is busy
     * (ie. once when the command and all of its parameters are received),
     * however, for Polygon and Line Rendering commands,
     * the bit gets cleared immediately after receiving the command word
     * (ie. before receiving the vertex parameters).
     * The bit is used as DMA request in DMA Mode 2, accordingly,
     * the DMA would probably hang if the Polygon/Line parameters,
     * are transferred in a separate DMA block (ie. the DMA probably starts ONLY on command words).
     */
    status |= readyForDmaBlock << 28;
    
    // Bits 29-30: DMA Direction (0=Off, 1=?, 2=CPUtoGP0, 3=GPUREADtoCPU)
    status |= (static_cast<uint32_t>(dmaDirection) & 0x03) << 29;

    // Bit 31: Drawing even/odd lines in interlace mode (0=Even, 1=Odd)
    //status |= uint32_t(isOddLine ? 1 : 0) << 31;
    static int test = 0;

    status |= (test % 2 == 1 ? 1 : 0) << 31;

    test++;
    
    /**
    * A(4Eh) - gpu_sync()
    * If DMA is off (when GPUSTAT.Bit29-30 are zero): Waits until GPUSTAT.Bit28=1 (or until timeout).
    * If DMA is on: Waits until D2_CHCR.Bit24=0 (or until timeout), and does then wait until GPUSTAT.Bit28=1 (without timeout, ie. may hang forever), and does then turn off DMA via GP1(04h).
    * Returns 0 (or -1 in case of timeout, however, the timeout values are very big, so it may take a LOT of seconds before it returns).
    */

    uint32_t dma = 0;
    if (dmaDirection == DmaDirection::Off) {
        dma = 0;
    } else if (dmaDirection == DmaDirection::Fifo) {
        dma = (readMode != VRam) ? 1u : 0u; // FIFO not more than half full. Approx until real FIFO level exists.
    } else if (dmaDirection == DmaDirection::CpuToGp0) {
        dma = readyForDmaBlock;
    } else if (dmaDirection == DmaDirection::VRamToCpu) {
        dma = readyToReadVram;
    }
    
    status |= dma << 25;
    
    return status;
}

bool Emulator::Gpu::readyToReceiveCommandWord() const {
    return readMode != VRam && gp0Mode == Command && gp0CommandRemaining == 0;
}

bool Emulator::Gpu::readyToSendVramToCpu() const { return readMode == VRam; }

bool Emulator::Gpu::readyToReceiveDmaBlock() const {
    if (readMode == VRam) {
        return false;
    }
    
    if (gp0Mode == VRam) {
        return true;
    }
    
    return gp0Mode == Command && gp0CommandRemaining == 0;
}

uint8_t lastCmd;
uint32_t lastOp;
std::deque<uint32_t> prvOps;
std::unordered_map<std::string, uint32_t> fields;
void Emulator::Gpu::gp0(uint32_t val) {
    // Testing
    if (gp0CommandRemaining > 0 && lastCmd != 0) {
        gp0CommandRemaining--;
        gp0Command.pushWord(val);
        
        // First command is gp0QuadMonoOpaque
        // which is the background of the boot screen
        if (gp0CommandRemaining == 0) {
            uint32_t in = gp0Command.index(0);
            
            switch (gp0Mode) {
                case Gp0Mode::Polygon: {
                    const bool gouraud   = fields["isGouraud"];
                    const bool fourVerts = fields["isFourVerts"];
                    const bool textured  = fields["isTextured"];

                    const bool isTextured = textured;
                    const uint8_t verticesCount = fourVerts ? 4 : 3;

                    Color c0 = Color::fromGp0(in);
                    Position positions[verticesCount];
                    Color colors[verticesCount];
                    UV uvs[verticesCount];

                    uint8_t idx = 0;
                    uint8_t stepping = 1 + (gouraud ? 1 : 0) + (isTextured ? 1 : 0);

                    uint16_t clut = isTextured ? gp0Command.index(2) >> 16 : 0;
                    uint16_t page = isTextured ? gp0Command.index(1 + stepping + 1) >> 16 : 0;

                    for (int i = 1; i < gp0Command.len; i += stepping) {
                        colors[idx] = (gouraud && i != 1) ? Color::fromGp0(gp0Command.index(i - 1)) : c0;

                        if (isTextured)
                            uvs[idx] = UV::fromGp0(gp0Command.index(i + 1), clut, page, *this);

                        positions[idx] = Position::fromGp0(gp0Command.index(i));
                        idx++;
                    }

                    curAttribute.setSemiTransparencyMode(semiTransparency);

                    if (fourVerts)
                        renderer->pushQuad(positions, colors, uvs, curAttribute);
                    else
                        renderer->pushTriangle(positions, colors, uvs, curAttribute);

                    break;
                    /*const bool gouraud         = fields["isGouraud"];
                    const bool fourVerts       = fields["isFourVerts"];
                    const bool textured  = fields["isTextured"];
                    const bool rawTexture = textured && fields["isRawTexture"];

                    const bool isTextured = textured;
                    
                    const uint8_t verticesCount = fourVerts ? 4 : 3;
                    
                    Color c0 = Color::fromGp0(in);
                    Position positions[verticesCount];
                    Color colors[verticesCount];
                    UV uvs[verticesCount];
                    
                    uint8_t idx = 0;
                    uint8_t stepping = 1 + gouraud + (isTextured ? 1 : 0);
                    
                    uint16_t clut = isTextured ? gp0Command.index(1 + 1) >> 16 : 0;
                    uint16_t page = isTextured ? gp0Command.index(1 + stepping + 1) >> 16 : 0;
                    
                    for (int i = 1; i < gp0Command.len; i += stepping) {
                        if (gouraud && i != 1) {
                            // It says, if doing flat shading then,
                            // there is no more colors to be sent per vertex
                            colors[idx] = Color::fromGp0(gp0Command.index(i - 1));
                        } else {
                            colors[idx] = c0;
                        }
                        
                        if (isTextured) {
                            uvs[idx] = UV::fromGp0(gp0Command.index(i + 1), clut, page, *this);
                        }
                        
                        positions[idx] = Position::fromGp0(gp0Command.index(i));
                        
                        idx++;
                    }
                    
                    if (fourVerts) {
                        renderer->pushQuad(positions, colors, uvs, curAttribute);
                    } else {
                        renderer->pushTriangle(positions, colors, uvs, curAttribute);
                    }*/
                }
                case Gp0Mode::Line: {
                    
                    break;
                }
                case Gp0Mode::Rectangle: {
                    const bool textured = fields["isTextured"] != 0;
                    const bool rawTexture = textured && fields["isRawTexture"];
                    const bool isTextured = textured;

                    const uint8_t rectangleSize   = fields["rectangleSize"];
                    
                    const Position p0 = Position::fromGp0(gp0Command.index(1));
                    const Color c0    = Color::fromGp0(in);
                    UV uv0;
                    
                    if (isTextured) {
                        const auto c = static_cast<uint16_t>(gp0Command.index(2) >> 16);
                        uv0 = UV::fromGp0(gp0Command.index(2), c, pageBaseX, pageBaseY, *this);
                    }
                    
                    uint16_t width = 0;
                    uint16_t height = 0;
                    
                    /**
                     * 0 (00)      variable size
                     * 1 (01)      single pixel (1x1)
                     * 2 (10)      8x8 sprite
                     * 3 (11)      16x16 sprite
                     */
                    if (rectangleSize == 0) {
                        uint32_t sizeData = gp0Command.index(isTextured ? 3 : 2);

                        width = sizeData & 0xFFFF;
                        height = sizeData >> 16;

                        // Max size of 1023x511; oversized rectangles are silently dropped.
                        if (width > 1023 || height > 511) {
                            break;
                        }
                    } else if (rectangleSize == 1) {
                        width = height = 1;
                    } else if (rectangleSize == 2) {
                        width = height = 8;
                    } else if (rectangleSize == 3) {
                        width = height = 16;
                    }

                    //uint8_t opcode = (in >> 24) & 0xFF;
                    curAttribute.setSemiTransparencyMode(semiTransparency);
                    renderRectangle(p0, c0, uv0, width, height);

                    break;
                }
            }
            
            gp0Mode = Command;
        }
        
        return;
    }
    
    if(gp0CommandRemaining == 0 && gp0Mode == Command) {
        gp0Command.clear();
        
        uint8_t opcode = (val >> 24) & 0xFF;
        uint8_t cmd = (val >> 29) & 0x7;
        
        // Polygon
        if (cmd == 0b001) {
            lastCmd = cmd;
            
            fields = decodeFields(val, polygonFields, std::size(polygonFields));
            gp0Mode = Gp0Mode::Polygon;

            bool gouraud         = fields["isGouraud"];
            bool fourVerts       = fields["isFourVerts"];
            bool semiTransparent = fields["isSemiTransparent"];
            bool textured        = fields["isTextured"];
            bool rawTexture      = textured && fields["isRawTexture"];
            
            TextureMode mode;
            if (!textured)
                mode = ColorOnly;
            else if (rawTexture)
                mode = TextureOnly;
            else
                mode = TextureColor;
            
            // IDE having weird issue so I gotta do a check
            curAttribute = { semiTransparent, textured && !rawTexture, mode };
            
            uint8_t verticesCount = fourVerts ? 4 : 3;
            bool isTextured = textured || rawTexture;
            
            uint8_t colorsCount = gouraud ? (verticesCount - 1) : 0;
            uint8_t uvsCount = isTextured ? (verticesCount) : 0;

            gp0CommandRemaining = (verticesCount + colorsCount + uvsCount);
            gp0Command.pushWord(val);
            
            return;
        } /*else if (cmd == 0b010) {
            //lastCmd = cmd;
            
            //fields = decodeFields(val, lineFields, std::size(lineFields));
            //gp0Mode = Gp0Mode::Line;
        }*/ else if (cmd == 0b011) {
            lastCmd = cmd;
            
            fields = decodeFields(val, rectangleFields, std::size(rectangleFields));
            gp0Mode = Gp0Mode::Rectangle;
            
            // gp0VarRectangleMonoOpaque
            bool semiTransparent = fields["isSemiTransparent"] != 0;
            bool textured        = fields["isTextured"] != 0;
            bool rawTexture      = textured && fields["isRawTexture"];
            uint8_t rectangleSize = fields["rectangleSize"];
            
            TextureMode mode;
            if (!textured)
                mode = ColorOnly;
            else if (rawTexture)
                mode = TextureOnly;
            else
                mode = TextureColor;
            
            // Rectangle doesn't have blending
            curAttribute = { semiTransparent, textured && !rawTexture, mode};

            curAttribute.setTextureDepth(static_cast<int>(textureDepth));
            
            // If it's a variable size then,
            // it'll contain an extra word
            uint8_t verticesCount = 1 + ((rectangleSize == 0) ? 1 : 0) + (textured ? 1 : 0);
            
            gp0CommandRemaining = verticesCount;
            gp0Command.pushWord(val);
            
            return;
        }
        
        if (cmd != 0 && cmd != 4 && cmd != 5 && cmd != 6 && cmd != 7) {
            //printf("f\n");
        }
        
        lastCmd = 0;
        
        switch (opcode) {
            case 0x00:
                gp0CommandRemaining = 0;
                Gp0CommandMethod    = &Gpu::gp0Nop;
                
                return;
            case 0x01:
                gp0CommandRemaining = 1;
                Gp0CommandMethod    = &Gpu::gp0ClearCache;
                
                break;
            case 0x1F:
                //   GP0(1Fh)                 - Interrupt Request (IRQ1)
                
                interrupt = true;
                // IRQ::trigger(IRQ::GPU);
                // assert(false);
                
                return;
            case 0x02:
                
                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp0FillVRam;
                
                break;
            case 0x03:
                //   GP0(03h)                 - Unknown (does take up FIFO space!!!)
                
                printf("WTF? CMD; = %x\n", cmd);
                
                return;
            case 0x20: /*
             case 0x21:*/
            {
                // GP0(20h) - Monochrome three-point polygon, opaque

                gp0CommandRemaining = 4;
                Gp0CommandMethod    = &Gpu::gp0TriangleMonoOpaque;

                break;
            }
            case 0x22: /*
             case 0x23:*/
            {
                // GP0(22h) - Monochrome three-point polygon, semi-transparent

                curAttribute = {1, 0, ColorOnly};

                gp0CommandRemaining = 4;
                Gp0CommandMethod    = &Gpu::gp0TriangleMonoOpaque;

                break;
            }
            case 0x24: {
                // GP0(24h) - Textured three-point polygon, opaque, texture-blending

                curAttribute = {0, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 7;
                Gp0CommandMethod    = &Gpu::gp0TriangleTexturedOpaque;

                break;
            }
            case 0x25: {
                // GP0(25h) - Textured three-point polygon, opaque, raw-texture

                curAttribute = {0, 0, TextureMode::TextureOnly};

                gp0CommandRemaining = 7;
                Gp0CommandMethod    = &Gpu::gp0TriangleRawTexturedOpaque;

                break;
            }
            case 0x26: {
                // GP0(26h) - Textured three-point polygon, semi-transparent, texture-blending

                curAttribute = {1, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 7;
                Gp0CommandMethod    = &Gpu::gp0TriangleTexturedOpaque;

                break;
            }
            case 0x27: {
                // GP0(27h) - Textured three-point polygon, semi-transparent, raw-texture

                curAttribute = {1, 0, TextureMode::TextureOnly};

                gp0CommandRemaining = 7;
                Gp0CommandMethod    = &Gpu::gp0TriangleRawTexturedOpaque;

                break;
            }
            case 0x2C: {
                // GP0(2Ch) - Textured four-point polygon, opaque, texture-blending
                // Used to draw the sony text & the text from the menus

                curAttribute = {0, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 9;
                Gp0CommandMethod    = &Gpu::gp0QuadTextureBlendOpaque;

                break;
            }
            case 0x2D: {
                // GP0(2Dh) - Textured four-point polygon, opaque, raw-texture
                // Used to draw the background of buttons in the menu

                curAttribute = {0, 0, TextureMode::TextureOnly};

                gp0CommandRemaining = 9;
                Gp0CommandMethod    = &Gpu::gp0QuadRawTextureBlendOpaque;

                break;
            }
            case 0x2E: {
                // GP0(2Eh) - Textured four-point polygon, semi-transparent, texture-blending

                curAttribute = {1, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 9;
                Gp0CommandMethod    = &Gpu::gp0QuadTextureBlendOpaque;

                break;
            }
            case 0x2F: {
                // GP0(2Fh) - Textured four-point polygon, semi-transparent, raw-texture

                curAttribute = {1, 0, TextureMode::TextureOnly};

                gp0CommandRemaining = 9;
                Gp0CommandMethod    = &Gpu::gp0QuadRawTextureBlendOpaque;

                break;
            }
            case 0x28:
                // GP0(28h) - Monochrome four-point polygon, opaque
                // Used to draw the background
                
                curAttribute = {0, 0, TextureMode::ColorOnly};
                
                gp0CommandRemaining = 5;
                Gp0CommandMethod    = &Gpu::gp0QuadMonoOpaque;
                
                break;
            case 0x29: {
                // TODO; Check why tf tekken 3 calls this
                // Undocumented/Nonsense
                /* 
                 * These commands have texture-blending disabled,
                 * which is nonsense because they are using untextured polygons anyways,
                 * ie. they are probably same as GP0(20h,22h,28h,2Ah,30h,32h,38h,3Ah).
                 */
                curAttribute = {0, 0, TextureMode::ColorOnly};
                
                gp0CommandRemaining = 5;
                Gp0CommandMethod    = &Gpu::gp0QuadMonoOpaque;

                break;
            }
            case 0x2A: /*
            case 0x2B: */
            {
                // GP0(2Ah) - Monochrome four-point polygon, semi-transparent

                curAttribute = {true, false, ColorOnly};

                gp0CommandRemaining = 5;
                Gp0CommandMethod    = &Gpu::gp0QuadMonoOpaque;

                break;
            }
            case 0x30: /*
             case 0x31:*/
            {
                // GP0(30h) - Shaded three-point polygon, opaque
                // Used to draw the diamond(2 triangles)

                curAttribute = {0, 0, ColorOnly};

                gp0CommandRemaining = 6;
                Gp0CommandMethod    = &Gpu::gp0TriangleShadedOpaque;

                break;
            }
            case 0x32: /*
             case 0x33:*/
            {
                // GP0(32h) - Shaded three-point polygon, semi-transparent

                curAttribute = {1, 0, ColorOnly};

                gp0CommandRemaining = 6;
                Gp0CommandMethod    = &Gpu::gp0TriangleShadedOpaque;

                break;
            }
            case 0x34: /*
             case 0x35:*/
            {
                // GP0(34h) - Shaded Textured three-point polygon, opaque, texture-blending

                curAttribute = {0, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 9;
                Gp0CommandMethod    = &Gpu::gp0TriangleTexturedShadedOpaque;

                break;
            }
            case 0x36: /*
             case 0x37:*/
            {
                // GP0(36h) - Shaded Textured three-point polygon, semi-transparent, tex-blend

                curAttribute = {1, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 9;
                Gp0CommandMethod    = &Gpu::gp0TriangleTexturedShadedOpaque;

                break;
            }
            case 0x38: /*
             case 0x39:*/
                // GP0(38h) - Shaded four-point polygon, opaque
                // Used to draw the diamond background, and the unique colored triangles

                curAttribute = {0, 0, TextureMode::ColorOnly};

                gp0CommandRemaining = 8;
                Gp0CommandMethod    = &Gpu::gp0QuadShadedOpaque;

                break;
            case 0x3A:
            case 0x3B:
                // GP0(3Ah) - Shaded four-point polygon, semi-transparent

                curAttribute = {1, 0, TextureMode::ColorOnly};

                gp0CommandRemaining = 8;
                Gp0CommandMethod    = &Gpu::gp0QuadShadedOpaque;

                break;
            case 0x3C:
            case 0x3D: {
                // GP0(3Ch) - Shaded Textured four-point polygon, opaque, texture-blending

                curAttribute = {0, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 12;
                Gp0CommandMethod    = &Gpu::gp0QuadTexturedShadedOpaque;

                break;
            }
            case 0x3E:
            case 0x3F: {
                // GP0(3Eh) - Shaded Textured four-point polygon, semi-transparent, tex-blend

                curAttribute = {1, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 12;
                Gp0CommandMethod    = &Gpu::gp0QuadTexturedShadedOpaque;

                break;
            }
            case 0x40: {
                //  GP0(40h) - Monochrome line, opaque

                curAttribute = {0, 0, TextureMode::ColorOnly};

                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp0MonoLine;

                break;
            }
            case 0x42: {
                //  GP0(42h) - Monochrome line, semi-transparent

                curAttribute = {1, 1, TextureMode::ColorOnly};

                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp0MonoLine;

                break;
            }
            case 0x48: {
                // GP0(48h) - Monochrome Poly-line, opaque

                curAttribute = {0, 0, TextureMode::ColorOnly};

                gp0CommandRemaining = 1;
                Gp0CommandMethod    = &Gpu::gp0PolyLineMono;
                gp0Mode             = PolyLine;

                break;
            }
            case 0x4A: {
                // GP0(4Ah) - Monochrome Poly-line, semi-transparent

                curAttribute = {1, 0, TextureMode::ColorOnly};

                gp0CommandRemaining = 1;
                Gp0CommandMethod    = &Gpu::gp0PolyLineMono;
                gp0Mode             = PolyLine;

                break;
            }
            case 0x50: {
                //  GP0(50h) - Shaded line, opaque
                curAttribute = {0, 0, TextureMode::ColorOnly};

                gp0CommandRemaining = 4;
                Gp0CommandMethod    = &Gpu::gp0ShadedLine;

                break;
            }
            case 0x52: {
                //  GP0(52h) - Shaded line, semi-transparent
                curAttribute = {1, 0, TextureMode::ColorOnly};

                gp0CommandRemaining = 4;
                Gp0CommandMethod    = &Gpu::gp0ShadedLine;

                break;
            }
            case 0x58: {
                //  GP0(58h) - Shaded Poly-line, opaque

                curAttribute = {0, 0, TextureMode::ColorOnly};

                gp0CommandRemaining = 1;
                Gp0CommandMethod    = &Gpu::gp0ShadedPolyLine;
                gp0Mode             = PolyLine;

                break;
            }
            case 0x5A: {
                // GP0(5Ah) - Shaded Poly-line, semi-transparent

                curAttribute = {1, 0, TextureMode::ColorOnly};

                gp0CommandRemaining = 1;
                Gp0CommandMethod    = &Gpu::gp0ShadedPolyLine;
                gp0Mode             = PolyLine;

                break;
            }
            case 0x60: {
                // GP0(60h) - Monochrome Rectangle (variable size) (opaque)

                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp0VarRectangleMonoOpaque;

                break;
            }
            case 0x62: {
                // GP0(62h) - Monochrome Rectangle (variable size) (semi-transparent)

                curAttribute = {true, false, ColorOnly};

                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp0VarRectangleMonoOpaque;

                break;
            }
            case 0x64: {
                // GP0(64h) - Textured Rectangle, variable size, opaque, texture-blending

                curAttribute = {0, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 4;
                Gp0CommandMethod    = &Gpu::gp0VarTexturedRectangleMonoOpaque;

                break;
            }
            case 0x65: {
                // GP0(65h) - Textured Rectangle, variable size, opaque, raw-texture
                // Used to draw title screens

                curAttribute = {0, 0, TextureMode::TextureOnly};

                gp0CommandRemaining = 4;
                Gp0CommandMethod    = &Gpu::gp0VarTexturedRectangleMonoOpaque;

                break;
            }
            case 0x66: {
                // GP0(66h) - Textured Rectangle, variable size, semi-transp, texture-blendin

                curAttribute = {1, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 4;
                Gp0CommandMethod    = &Gpu::gp0VarTexturedRectangleMonoOpaque;

                break;
            }
            case 0x67: {
                // GP0(67h) - Textured Rectangle, variable size, semi-transp, raw-texture

                curAttribute = {1, 0, TextureMode::TextureOnly};

                gp0CommandRemaining = 4;
                Gp0CommandMethod    = &Gpu::gp0VarTexturedRectangleMonoOpaque;

                break;
            }
            case 0x68: {
                // GP0(68h) - Monochrome Rectangle (1x1) (Dot) (opaque)

                gp0CommandRemaining = 2;
                Gp0CommandMethod    = &Gpu::gp0DotRectangleMonoOpaque;

                break;
            }
            case 0x6A: {
                // GP0(6Ah) - Monochrome Rectangle (1x1) (Dot) (semi-transparent)

                curAttribute = {true, false, ColorOnly};

                gp0CommandRemaining = 2;
                Gp0CommandMethod    = &Gpu::gp0DotRectangleMonoOpaque;

                break;
            }
            case 0x70: {
                // GP0(70h) - Monochrome Rectangle (8x8) (opaque)

                gp0CommandRemaining = 2;
                Gp0CommandMethod    = &Gpu::gp08RectangleMonoOpaque;

                break;
            }
            case 0x72: {
                // GP0(72h) - Monochrome Rectangle (8x8) (semi-transparent)

                curAttribute = {true, false, ColorOnly};

                gp0CommandRemaining = 2;
                Gp0CommandMethod    = &Gpu::gp08RectangleMonoOpaque;

                break;
            }
            case 0x74: {
                // GP0(74h) - Textured Rectangle, 8x8, opaque, texture-blending

                curAttribute = {0, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp08RectangleTexturedOpaqu;

                break;
            }
            case 0x75: {
                // GP0(75h) - Textured Rectangle, 8x8, opaque, raw-texture

                curAttribute = {0, 0, TextureMode::TextureOnly};

                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp08RectangleTexturedOpaqu;

                break;
            }
            case 0x76: {
                // GP0(76h) - Textured Rectangle, 8x8, semi-transparent, texture-blending

                curAttribute = {1, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp08RectangleTexturedOpaqu;

                break;
            }
            case 0x77: {
                // GP0(77h) - Textured Rectangle, 8x8, semi-transparent, raw-texture

                curAttribute = {1, 0, TextureMode::TextureOnly};

                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp08RectangleTexturedOpaqu;

                break;
            }
            case 0x78: {
                // GP0(78h) - Monochrome Rectangle (16x16) (opaque)

                gp0CommandRemaining = 2;
                Gp0CommandMethod    = &Gpu::gp016RectangleMonoOpaque;

                break;
            }
            case 0x7A: {
                // GP0(7Ah) - Monochrome Rectangle (16x16) (semi-transparent)

                curAttribute = {true, false, ColorOnly};

                gp0CommandRemaining = 2;
                Gp0CommandMethod    = &Gpu::gp016RectangleMonoOpaque;

                break;
            }
            case 0x7C: {
                // GP0(7Ch) - Textured Rectangle, 16x16, opaque, texture-blending

                curAttribute = {0, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp016RectangleTextured;

                break;
            }
            case 0x7D: {
                // GP0(7Dh) - Textured Rectangle, 16x16, opaque, raw-texture

                curAttribute = {0, 1, TextureMode::TextureOnly};

                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp016RectangleTextured;

                break;
            }
            case 0x7E: {
                // GP0(7Eh) - Textured Rectangle, 16x16, semi-transparent, texture-blending

                curAttribute = {1, 1, TextureMode::TextureColor};

                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp016RectangleTextured;

                break;
            }
            case 0x7F: {
                // GP0(7Fh) - Textured Rectangle, 16x16, semi-transparent, raw-texture
                
                curAttribute = {1, 0, TextureMode::TextureOnly};
                
                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp016RectangleTextured;
                
                break;
            }
            case 0x80: {
                gp0CommandRemaining = 4;
                Gp0CommandMethod    = &Gpu::gp0VramToVram;
                
                break;
            }
            case 0xA0:
                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp0ImageLoad;
                
                break;
            case 0xC0:
            case 0xC1:
            case 0xC2:
            case 0xC3:
            case 0xC4:
            case 0xC5:
            case 0xC6:
            case 0xC7:
            case 0xC8:
            case 0xC9:
            case 0xCA:
            case 0xCB:
            case 0xCC:
            case 0xCD:
            case 0xCE:
            case 0xCF:
            case 0xD0:
            case 0xD1:
            case 0xD2:
            case 0xD3:
            case 0xD4:
            case 0xD5:
            case 0xD6:
            case 0xD7:
            case 0xD8:
            case 0xD9:
            case 0xDA:
            case 0xDB:
            case 0xDC:
            case 0xDD:
            case 0xDE:
            case 0xDF:
                gp0CommandRemaining = 3;
                Gp0CommandMethod    = &Gpu::gp0ImageStore;
                
                break;
            case 0xE1:
                gp0CommandRemaining = 1;
                Gp0CommandMethod    = &Gpu::gp0DrawMode;
                
                break;
            case 0xE2:
                gp0CommandRemaining = 1;
                Gp0CommandMethod    = &Gpu::gp0TextureWindow;
                
                break;
            case 0xE3:
                gp0CommandRemaining = 1;
                Gp0CommandMethod    = &Gpu::gp0DrawingAreaTopLeft;
                
                break;
            case 0xE4:
                gp0CommandRemaining = 1;
                Gp0CommandMethod    = &Gpu::gp0DrawingAreaBottomRight;
                
                break;
            case 0xE5:
                gp0CommandRemaining = 1;
                Gp0CommandMethod    = &Gpu::gp0DrawingOffset;
                
                break;
            case 0xE6:
                gp0CommandRemaining = 1;
                Gp0CommandMethod    = &Gpu::gp0MaskBitSetting;
                
                break;
            default:
                // TODO; idk probably doing smth wrong but Tekken 3 calls those but they dont exists?
                
                /**
                 * GP0(35h,37h,3Dh,3Fh) - Undocumented/Nonsense (Raw Texture + UNUSED shading)
                 * These are undocumented inefficient nonsense commands: Parameters are same as for
                 * GP0(34h,36h,3Ch,3Eh), ie. with colors for all vertices, but without actually using that colors.
                 * Instead, the commands are rendering raw textures without blending. In other words, the commands have
                 * same function as GP0(25h,27h,2Dh,2Fh), but with additional/unused parameters (and possible
                 * additional/unused internal gouraud shading calculations). For whatever reason, Castlevania is
                 * actually using these nonsense commands, namely GP0(3Dh) and GP0(3Fh).
                 *
                 * GP0(21h,23h,29h,2Bh,31h,33h,39h,3Bh) - Undocumented/Nonsense
                 * These commands have texture-blending disabled, which is nonsense because they are using untextured
                 * polygons anyways, ie. they are probably same as GP0(20h,22h,28h,2Ah,30h,32h,38h,3Ah).
                 */
                printf("Unhandled GP0 command %x last; %x cmd? = %x\n", opcode, lastOp, cmd);
                
                return;
        }
        
        lastOp = opcode;
        prvOps.push_front(opcode);
        
        if (prvOps.size() > 7)
            prvOps.pop_back();
    }
    
    switch (gp0Mode) {
        case Command:
            gp0CommandRemaining--;
            gp0Command.pushWord(val);
            
            if(gp0CommandRemaining == 0) {
                // Set texture depth of the vertices that are going,
                // to be sent to the GPU
                curAttribute.setTextureDepth(static_cast<int>(this->textureDepth));
                curAttribute.setSemiTransparencyMode(semiTransparency);
                
                // All of the parameters optioned; run the command
                Gp0CommandMethod(*this, val);
                
                // Reset current attributes
                curAttribute = { 0, 0, TextureMode::ColorOnly };
            }
            
            break;
        case VRam: {
            const auto step = [&]() {
                if (++curX >= endX) {
                    curX = startX;
                    
                    if (++curY >= endY) {
                        // Signal VRAM that image has finished loading.
                        //vram->endTransfer();
                        
                        gp0CommandRemaining = 0;
                        gp0Mode = Command;
                        
                        uint32_t w = endX - startX;
                        uint32_t h = endY - startY;
                        
                        vram->flushRegion(startX, startY, w, h);
                        
                        uint32_t glY = (512 - startY) - h;
                        
                        renderer->flushDrawCommands();
                        vram->copyToTexture(startX, glY, startX, glY, w, h, renderer->sceneTex[renderer->curTex]);
                        
                        return true;
                    }
                }
                
                return false;
            };
            
            /*const auto finish = [&] {
                gp0CommandRemaining = 0;
                gp0Mode = Command;
                
                /*if (displayDepth == DisplayDepth::D24Bits)
                    printf("DISPLAY 24BIT\n");
                else
                    printf("DISPLAY 15BIT\n");#1#
                
                if (!renderVRamToScreen) return;
                
                uint32_t w = endX - startX;
                uint32_t h = endY - startY;
                
                vram->flushRegion(startX, startY, w, h);
                
                uint32_t glY = (512 - startY) - h;
                
                //vram->copyToTexture(startX, glY, dstX, dstY, w, h, renderer->sceneTex[0]);
                vram->copyToTexture(startX, glY, startX, glY, w, h, renderer->sceneTex[0]);
            };
            */
            
            /*uint16_t p0 = val & 0xFFFF;
            uint16_t p1 = val >> 16;
            
            if (displayDepth == DisplayDepth::D24Bits) {
                vram->setPixel(curX, curY, p0);
            } else {
                vram->writePixel(curX, curY, p0);
            }
            
            curX++;
            
            if (curX >= endX) {
                curX = startX;
                curY++;
                
                if (curY >= endY) {
                    finish();
                    return;
                }
            }
            
            if (displayDepth == DisplayDepth::D24Bits) {
                vram->setPixel(curX, curY, p1);
            } else {
                vram->writePixel(curX, curY, p1);
            }
            
            curX++;
            
            if (curX >= endX) {
                curX = startX;
                curY++;
                
                if (curY >= endY)
                    finish();
            }*/
            
            /*uint16_t p0 = val & 0xFFFF;
            uint16_t p1 = val >> 16;
            vram->writePixel(curX, curY, p0);

            curX++;
            if (curX >= endX) {
                curX = startX;
                curY++;
                if (curY >= endY) finish();
            }

            vram->writePixel(curX, curY, p1);

            curX++;
            if (curX >= endX) {
                curX = startX;
                curY++;
                if (curY >= endY) finish();
            }*/
            
            if (displayDepth == DisplayDepth::D24Bits) {
                vram->setPixel(curX, curY, val & 0xFFFF);
                if(step()) {
                    printf("");
                }
                
                vram->setPixel(curX, curY, (val >> 16) & 0xFFFF);
                if(step()) {
                    printf("");
                }
                
                if (startY24 >= endY) {
                    gp0CommandRemaining = 0;
                    gp0Mode = Command;
                    //vram->reset();
                }
            } else {
                vram->setPixel(curX, curY, val & 0xFFFF);
                if(step()) return;
                
                vram->setPixel(curX, curY, (val >> 16) & 0xFFFF);
                if(step()) return;
            }
            break;
        }
        
        case PolyLine: {
            /*
             * Termination Codes for Poly-Lines (aka Linestrips)
             * The termination code should be usually 55555555h, however,
             * Wild Arms 2 uses 50005000h (unknown which exact bits/values are relevant there).
             */
            if((val & 0xf000f000) == 0x50005000) {
                // Set texture depth of the vertices that are going,
                // to be sent to the GPU
                curAttribute.setTextureDepth(static_cast<int>(this->textureDepth));
                curAttribute.setSemiTransparencyMode(semiTransparency);
                
                // Termination code
                Gp0CommandMethod(*this, val);
                
                gp0Mode = Command;
                gp0CommandRemaining = 0;
                
                // Reset current attributes
                curAttribute = { 0, 0, TextureMode::ColorOnly };
                
                return;
            }
            
            gp0Command.pushWord(val);
            
            // Just in case..
            gp0CommandRemaining = 1;
            
            break;
        }
        
        break;
    }
}

void Emulator::Gpu::gp0DrawMode(uint32_t val) {
    renderer->flushDrawCommands();

    // https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#clut-attribute-color-lookup-table-aka-palette
    pageBaseX = static_cast<uint8_t>((val & 0xF));
    pageBaseY = static_cast<uint8_t>((val >> 4) & 1);

    // (0=B/2+F/2, 1=B+F, 2=B-F, 3=B+F/4)
    semiTransparency = static_cast<uint8_t>((val >> 5) & 3);

    switch ((val >> 7) & 3) {
        case 0:
            textureDepth = TextureDepth::T4Bit;
            break;
        case 1:
            textureDepth = TextureDepth::T8Bit;
            break;
        case 2: case 3:
            // Texture page colors setting 3 (reserved) is same as setting 2 (15bit).
            textureDepth = TextureDepth::T15Bit;
            break;
        default:
            throw std::runtime_error("Unhandled texture depth " + std::to_string((val >> 7) & 3));
    }

    // Upload texture depth to GPU
    /*renderer->setTextureDepth(static_cast<int>(textureDepth));*/
    setTextureDepth(textureDepth);
    renderer->setSemiTransparencyMode(semiTransparency);

    // Dither 24bit to 15bit (0=Off/strip LSBs, 1=Dither Enabled) ;GPUSTAT.9
    dithering = ((val >> 9) & 1) != 0;
    drawToDisplay = ((val >> 10) & 1) != 0; // (0=Prohibited, 1=Allowed)
    textureDisable = newTextureDisable && (((val >> 11) & 1) != 0);
    rectangleTextureFlipX = ((val >> 12) & 1) != 0;
    rectangleTextureFlipY = ((val >> 13) & 1) != 0;
}

void Emulator::Gpu::gp0DrawingAreaTopLeft(uint32_t val) {
    renderer->flushDrawCommands();

    drawingAreaTop = static_cast<int16_t>((val >> 10) & 0x3FF); // Y: bits 10-19
    drawingAreaLeft = static_cast<int16_t>(val & 0x3FF);         // X: bits 0-9

    renderer->setDrawingArea(drawingAreaLeft, drawingAreaRight, drawingAreaTop, drawingAreaBottom);
}

void Emulator::Gpu::gp0DrawingAreaBottomRight(uint32_t val) {
    renderer->flushDrawCommands();

    drawingAreaBottom = static_cast<uint16_t>((val >> 10) & 0x3FF); // Y: bits 10-19
    drawingAreaRight  = static_cast<uint16_t>(val & 0x3FF);         // X: bits 0-9

    // uint16_t width  = std::max(drawingAreaLeft, drawingAreaRight) - std::min(drawingAreaLeft, drawingAreaRight) + 1;
    // uint16_t height = std::max(drawingAreaTop, drawingAreaBottom) - std::min(drawingAreaTop, drawingAreaBottom) + 1;

    // printf("So; %d - %d =? %d\n", drawingAreaRight, drawingAreaBottom, width);

    // TODO;
    // renderer->setDrawingArea(0, 0, width, height);
    renderer->setDrawingArea(drawingAreaLeft, drawingAreaRight, drawingAreaTop, drawingAreaBottom);
    // renderer->setDrawingArea(0, 0, 1024, 512);
}

void Emulator::Gpu::gp0DrawingOffset(const uint32_t val) const {
    // bits 0..10  = X offset (11-bit signed)
    // bits 11..21 = Y offset (11-bit signed)
    /*auto signExtend11 = [](uint32_t v) -> int32_t {
        v &= 0x7FFu;                // keep 11 bits
        
        if (v & 0x400u) {           // sign bit (bit 10)
            return int32_t(v | ~0x7FFu); // set upper bits to 1
        }
        
        return int32_t(v);
    };
    
    int32_t x = signExtend11(val >> 0);
    int32_t y = signExtend11(val >> 11);
    
    drawingXOffset = x << 16;
    drawingYOffset = y << 16;*/
    
    //GTE::of[0] += (int64_t)drawingXOffset;
    //GTE::of[1] += (int64_t)drawingYOffset;
    
    uint16_t x = val & 0x7FF;               // bits 0–10 (11 bits)
    uint16_t y = (val >> 11) & 0x7FF;       // bits 11–21 (11 bits)
    
    // Values are 11bit two's complement-signed values,
    // we need to shift the value to 16 bits,
    // to force a sign extension
    drawingXOffset = static_cast<int16_t>(x << 5) >> 5;
    drawingYOffset = static_cast<int16_t>(y << 5) >> 5;

    // printf("So; %d - %d from: {%u}\n", drawingXOffset, drawingYOffset, val);

    // Update rendering offset
    // renderer->setDrawingOffset(drawingXOffset, drawingYOffset);
}

void Emulator::Gpu::gp0TextureWindow(uint32_t val) {
    renderer->flushDrawCommands();

    textureWindowXMask   = static_cast<uint8_t>(val & 0x1F);
    textureWindowYMask   = static_cast<uint8_t>((val >> 5) & 0x1F);
    textureWindowXOffset = static_cast<uint8_t>((val >> 10) & 0x1F);
    textureWindowYOffset = static_cast<uint8_t>((val >> 15) & 0x1F);
    
    /*assert(textureWindowYMask == 0);
    assert(textureWindowXMask == 0);
    assert(textureWindowXOffset == 0);
    assert(textureWindowYOffset == 0);*/
    
    renderer->setTextureWindow(textureWindowXMask, textureWindowYMask, textureWindowXOffset, textureWindowYOffset);
}

void Emulator::Gpu::gp0MaskBitSetting(uint32_t val) {
    renderer->flushDrawCommands();

    forceSetMaskBit = (val & 1) != 0;
    preserveMaskedPixels = (val & 2) != 0;
}

void Emulator::Gpu::gp0QuadMonoOpaque(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.buffer[1]),
        Position::fromGp0(gp0Command.buffer[2]),
        Position::fromGp0(gp0Command.buffer[3]),
        Position::fromGp0(gp0Command.buffer[4]),
    };
    
    // A single color repeated 4 times
    Color colors[] = {
        Color::fromGp0(gp0Command.buffer[0]),
        Color::fromGp0(gp0Command.buffer[0]),
        Color::fromGp0(gp0Command.buffer[0]),
        Color::fromGp0(gp0Command.buffer[0]),
    };
    
    renderer->pushQuad(positions, colors, {}, curAttribute);
}

void Emulator::Gpu::gp0TriangleShadedOpaque(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.buffer[1]),
        Position::fromGp0(gp0Command.buffer[3]),
        Position::fromGp0(gp0Command.buffer[5]),
    };
    
    Color colors[] = {
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(2)),
        Color::fromGp0(gp0Command.index(4)),
    };
    
    renderer->pushTriangle(positions, colors, {}, curAttribute);
}

void Emulator::Gpu::gp0TriangleTexturedShadedOpaque(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.buffer[1]),
        Position::fromGp0(gp0Command.buffer[4]),
        Position::fromGp0(gp0Command.buffer[7]),
    };
    
    Color colors[] = {
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(3)),
        Color::fromGp0(gp0Command.index(6)),
    };
    
    uint16_t c = static_cast<uint16_t>(gp0Command.index(2) >> 16);
    uint16_t p = static_cast<uint16_t>(gp0Command.index(5) >> 16);
    
    UV uvs[] = {
        UV::fromGp0(gp0Command.index(2), c, p, *this),
        UV::fromGp0(gp0Command.index(5), c, p, *this),
        UV::fromGp0(gp0Command.index(8), c, p, *this),
    };
    
    renderer->pushTriangle(positions, colors, uvs, curAttribute);
}

void Emulator::Gpu::gp0QuadShadedOpaque(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.buffer[1]),
        Position::fromGp0(gp0Command.buffer[3]),
        Position::fromGp0(gp0Command.buffer[5]),
        Position::fromGp0(gp0Command.buffer[7]),
    };
    
    Color colors[] = {
        Color::fromGp0(gp0Command.buffer[0]),
        Color::fromGp0(gp0Command.buffer[2]),
        Color::fromGp0(gp0Command.buffer[4]),
        Color::fromGp0(gp0Command.buffer[6]),
    };
    
    renderer->pushQuad(positions, colors, {}, curAttribute);
}

void Emulator::Gpu::gp0QuadTexturedShadedOpaque(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.index(1)),
        Position::fromGp0(gp0Command.index(4)),
        Position::fromGp0(gp0Command.index(7)),
        Position::fromGp0(gp0Command.index(10)),
    };
    
    Color colors[] = {
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(3)),
        Color::fromGp0(gp0Command.index(6)),
        Color::fromGp0(gp0Command.index(9)),
    };
    
    uint16_t c = static_cast<uint16_t>(gp0Command.index(2) >> 16);
    uint16_t p = static_cast<uint16_t>(gp0Command.index(5) >> 16);
    
    UV uvs[] = {
        UV::fromGp0(gp0Command.index(2),  c, p, *this),
        UV::fromGp0(gp0Command.index(5),  c, p, *this),
        UV::fromGp0(gp0Command.index(8),  c, p, *this),
        UV::fromGp0(gp0Command.index(11), c, p, *this),
    };
    
    renderer->pushQuad(positions, colors, uvs, curAttribute);
}

void Emulator::Gpu::gp0MonoLine(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.index(1)),
        Position::fromGp0(gp0Command.index(2)),
    };
    
    Color colors[] = {
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(0)),
    };
    
    renderer->pushLine(positions, colors, {}, curAttribute);
}

void Emulator::Gpu::gp0PolyLineMono(uint32_t val) {
    if (gp0Command.len < 2) {
        std::cerr << "INVALID POLYLINE COMMAND!\n";
        return;
    }

    /**
     * TODO; If the 2 vertices in a line overlap,
     * TODO; then the GPU will draw a 1x1 rectangle,
     * TODO; in the location of the 2 vertices using the colour of the first vertex.
     */
    auto     currentLineColor = Color::fromGp0(gp0Command.index(0));
    Position prev             = Position::fromGp0(gp0Command.index(1));
    
    for (size_t i = 2; i < gp0Command.len; i++) {
        uint32_t data = gp0Command.index(i);
        
        Position current = Position::fromGp0(data);
        
        Position positions[] = {prev, current};
        Color colors[] = {currentLineColor, currentLineColor};
        
        renderer->pushLine(positions, colors, {}, curAttribute);
        
        prev = current;
    }
}

void Emulator::Gpu::gp0ShadedLine(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.index(1)),
        Position::fromGp0(gp0Command.index(3)),
    };
    
    Color colors[] = {
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(2)),
    };
    
    renderer->pushLine(positions, colors, {}, curAttribute);
}

void Emulator::Gpu::gp0ShadedPolyLine(uint32_t val) {
    if (/*gp0Command.len < 6 ||*/ (gp0Command.len & 1) != 0) {
        std::cerr << "INVALID SHADED POLYLINE COMMAND!\n";
        return;
    }

    /**
     * TODO; If the 2 vertices in a line overlap,
     * TODO; then the GPU will draw a 1x1 rectangle,
     * TODO; in the location of the 2 vertices using the colour of the first vertex.
     */
    Color    c0   = Color   ::fromGp0(gp0Command.index(0));
    Position p0 = Position::fromGp0(gp0Command.index(1));
    
    for (size_t i = 2; i < gp0Command.len; i += 2) {
        const Color    c1 = Color::fromGp0(gp0Command.index(i));
        const Position p1 = Position::fromGp0(gp0Command.index(i + 1));
        
        Position positions[] = {p0, p1};
        Color colors[] = {c0, c1};
        
        renderer->pushLine(positions, colors, {}, curAttribute);
        
        c0 = c1;
        p0 = p1;
    }
}

void Emulator::Gpu::renderRectangle(Position position, Color color, UV uv, uint16_t width, uint16_t height) {
    //position.x += drawingXOffset;
    //position.y += drawingYOffset; 
    
    /*Position positions[4] = {
        position,
        {position.x + width, position.y},
        {position.x + width, position.y + height},
        {position.x, position.y + height},
    };*/
    
    Position positions[4] = {
        position,
        {position.x + width, position.y},
        {position.x, position.y + height},
        {position.x + width, position.y + height},
    };
    
    Color colors[4] = { color, color, color, color };
    
    UV uvs[4] = {
        UV(uv.u          , uv.v           , uv.dataX, uv.dataY),
        UV(uv.u + width, uv.v           , uv.dataX, uv.dataY),
        UV(uv.u          , uv.v + height, uv.dataX, uv.dataY),
        UV(uv.u + width, uv.v + height, uv.dataX, uv.dataY),
    };
    
    if (rectangleTextureFlipX) std::swap(uvs[0], uvs[1]), std::swap(uvs[2], uvs[3]);
    if (rectangleTextureFlipY) std::swap(uvs[0], uvs[2]), std::swap(uvs[1], uvs[3]);
    
    renderer->pushRectangle(positions, colors, uvs, curAttribute);
}

void Emulator::Gpu::gp0VarRectangleMonoOpaque(uint32_t val) {
    Color color = Color::fromGp0(gp0Command.index(0));
    Position position = Position::fromGp0(gp0Command.index(1));
    
    uint32_t sizeData = gp0Command.index(2);
    
    uint16_t width = sizeData & 0xFFFF;
    uint16_t height = sizeData >> 16;
    
    renderRectangle(position, color, {}, width, height);
}

// 65
void Emulator::Gpu::gp0VarTexturedRectangleMonoOpaque(uint32_t val) {
    Color color = Color::fromGp0(gp0Command.index(0));
    Position position = Position::fromGp0(gp0Command.index(1));
    //position.x += drawingXOffset;
    //position.y += drawingYOffset;
    
    uint16_t c = static_cast<uint16_t>(gp0Command.index(2) >> 16);
    
    // Text page
    UV uv = UV::fromGp0(gp0Command.index(2), c, pageBaseX, pageBaseY, *this);
    
    uint32_t sizeData = gp0Command.index(3);
    
    uint16_t width = sizeData & 0xFFFF;
    uint16_t height = sizeData >> 16;
    
    renderRectangle(position, color, uv, width, height);
}

void Emulator::Gpu::gp0DotRectangleMonoOpaque(uint32_t val) {
    Color color = Color::fromGp0(gp0Command.index(0));
    Position position = Position::fromGp0(gp0Command.index(1));
    
    renderRectangle(position, color, {}, 1, 1);
}

void Emulator::Gpu::gp08RectangleMonoOpaque(uint32_t val) {
    Color color = Color::fromGp0(gp0Command.index(0));
    Position position = Position::fromGp0(gp0Command.index(1));
    
    renderRectangle(position, color, {}, 8, 8);
}

void Emulator::Gpu::gp08RectangleTexturedOpaqu(uint32_t val) {
    Color color = Color::fromGp0(gp0Command.index(0));
    Position position = Position::fromGp0(gp0Command.index(1));
    
    uint16_t c = static_cast<uint16_t>(gp0Command.index(2) >> 16);
    //uint16_t p = static_cast<uint16_t>(gp0Command.index(3) >> 16);
    
    UV uv = UV::fromGp0(gp0Command.index(2), c, pageBaseX, pageBaseY, *this);
    
    renderRectangle(position, color, uv, 8, 8);
}

void Emulator::Gpu::gp016RectangleMonoOpaque(uint32_t val) {
    Color color = Color::fromGp0(gp0Command.index(0));
    Position position = Position::fromGp0(gp0Command.index(1));
    
    renderRectangle(position, color, {}, 16, 16);
}

void Emulator::Gpu::gp016RectangleTextured(uint32_t val) {
    Color color = Color::fromGp0(gp0Command.index(0));
    Position position = Position::fromGp0(gp0Command.index(1));
    
    uint16_t c = static_cast<uint16_t>(gp0Command.index(2) >> 16);
    //uint16_t p = static_cast<uint16_t>(gp0Command.index(3) >> 16);
    
    UV uv = UV::fromGp0(gp0Command.index(2), c, pageBaseX, pageBaseY, *this);
    
    renderRectangle(position, color, uv, 16, 16);
}

void Emulator::Gpu::gp0ClearCache(uint32_t val) {
    //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    //renderer->clear();
    
    //printf("CLEAR\n");
}

void Emulator::Gpu::gp0FillVRam(uint32_t val) {
    //uint32_t color =  gp0Command.index(0) & 0xFFFFFF;
    uint32_t c = gp0Command.index(0);
    uint32_t cords = gp0Command.index(1);
    uint32_t res = gp0Command.index(2);
    
    Color c0 = Color::fromGp0(c);
    
    /*uint16_t r =  c        & 0x1F;
    uint16_t g = (c >> 5 ) & 0x1F;
    uint16_t b = (c >> 10) & 0x1F;
    
    uint16_t rgb555 = (b << 10) | (g << 5) | r;*/
    
    struct mask {
        constexpr static int startX(int x) { return x & 0x3f0; }
        constexpr static int startY(int y) { return y & 0x1ff; }
        constexpr static int endX(int x) { return ((x & 0x3ff) + 0x0f) & ~0x0f; }
        constexpr static int endY(int y) { return y & 0x1ff; }
    };
    
    startX = startX24 = curX = std::max<int>(0, mask::startX(cords & 0xffff));
    startY = startY24 = curY = std::max<int>(0, mask::startY((cords & 0xffff0000) >> 16));
    endX = std::min<int>(1024, startX + mask::endX(res & 0xffff));
    endY = std::min<int>(512, startY + mask::endY((res & 0xffff0000) >> 16));
    
    //assert(displayDepth == DisplayDepth::D24Bits && "Display depth is 24bits.. ;-;");
    
    // NAW I HAD ENDX AND ENDY SWAPPED
    for(uint32_t y = startY; y < endY; y++) {
        for(uint32_t x = startX; x < endX; x++) {
            vram->setPixel(x, y, c0.toU32());
        }
    }
    
    if (!renderVRamToScreen) return;
    //return; // TODO; Does weird shit
    // TODO; Uhh just copy area instead???

    uint32_t w = endX - startX;
    uint32_t h = endY - startY;

    vram->flushRegion(startX, startY, w, h);

    uint32_t glY = 512 - startY - h;
    renderer->flushDrawCommands();
    vram->copyToTexture(startX, glY, startX, glY, w, h, renderer->sceneTex[renderer->curTex]);
    
    /*Position positions[] = {
        { startX, startY },  // 0 TL
        { endX,   startY },  // 1 TR
        { endX,   endY   },  // 2 BR
        { startX, endY   }   // 3 BL
    };
    
    Color colors[4] = { c0, c0, c0, c0 };
    
    Attributes attr = { false, false, ColorOnly };
    
    // Triangle 1: 2,3,0
    {
        Position t1[3] = { positions[0], positions[1], positions[2] };
        Color    c1[3] = { colors[0],    colors[1],    colors[2]    };
        
        renderer->pushTriangle(t1, c1, {}, attr);
    }
    
    // Triangle 2: 3,0,1
    {
        Position t2[3] = { positions[0], positions[2], positions[3] };
        Color    c2[3] = { colors[0],    colors[2],    colors[3]    };
        
        renderer->pushTriangle(t2, c2, {}, attr);
    }*/
}

void Emulator::Gpu::gp0TriangleMonoOpaque(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.index(1)),
        Position::fromGp0(gp0Command.index(2)),
        Position::fromGp0(gp0Command.index(3)),
    };
    
    // Mono
    Color colors[] = {
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(0)),
    };
    
    renderer->pushTriangle(positions, colors, {}, curAttribute);
}

void Emulator::Gpu::gp0TriangleTexturedOpaque(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.index(1)),
        Position::fromGp0(gp0Command.index(3)),
        Position::fromGp0(gp0Command.index(5))
    };
    
    // This uses textures along side a color
    Color colors[] = {
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(0)),
    };
    
    uint16_t c = static_cast<uint16_t>(gp0Command.index(2) >> 16);
    uint16_t p = static_cast<uint16_t>(gp0Command.index(4) >> 16);
    
    UV uvs[] = {
        UV::fromGp0(gp0Command.index(2), c, p, *this),
        UV::fromGp0(gp0Command.index(4), c, p, *this),
        UV::fromGp0(gp0Command.index(6), c, p, *this),
    };
    
    renderer->pushTriangle(positions, colors, uvs, curAttribute);
}

void Emulator::Gpu::gp0TriangleRawTexturedOpaque(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.index(1)),
        Position::fromGp0(gp0Command.index(3)),
        Position::fromGp0(gp0Command.index(5))
    };
    
    uint16_t c = static_cast<uint16_t>(gp0Command.index(2) >> 16);
    uint16_t p = static_cast<uint16_t>(gp0Command.index(4) >> 16);
    
    UV uvs[] = {
        UV::fromGp0(gp0Command.index(2), c, p, *this),
        UV::fromGp0(gp0Command.index(4), c, p, *this),
        UV::fromGp0(gp0Command.index(6), c, p, *this),
    };
    
    renderer->pushTriangle(positions, {}, uvs, curAttribute);
}

void Emulator::Gpu::gp0QuadTextureBlendOpaque(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.index(1)),
        Position::fromGp0(gp0Command.index(3)),
        Position::fromGp0(gp0Command.index(5)),
        Position::fromGp0(gp0Command.index(7)),
    };
    
    Color colors[] = {
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(0)),
        Color::fromGp0(gp0Command.index(0)),
    };
    
    uint16_t c = static_cast<uint16_t>(gp0Command.index(2) >> 16);
    uint16_t p = static_cast<uint16_t>(gp0Command.index(4) >> 16);
    
    UV uvs[] = {
        UV::fromGp0(gp0Command.index(2), c, p, *this),
        UV::fromGp0(gp0Command.index(4), c, p, *this),
        UV::fromGp0(gp0Command.index(6), c, p, *this),
        UV::fromGp0(gp0Command.index(8), c, p, *this),
    };
    
    renderer->pushQuad(positions, colors, uvs, curAttribute);
}

// FF
void Emulator::Gpu::gp0QuadRawTextureBlendOpaque(uint32_t val) {
    Position positions[] = {
        Position::fromGp0(gp0Command.index(1)),
        Position::fromGp0(gp0Command.index(3)),
        Position::fromGp0(gp0Command.index(5)),
        Position::fromGp0(gp0Command.index(7)),
    };
    
    uint16_t c = static_cast<uint16_t>(gp0Command.index(2) >> 16);
    uint16_t p = static_cast<uint16_t>(gp0Command.index(4) >> 16);
    
    UV uvs[] = {
        UV::fromGp0(gp0Command.index(2), c, p, *this),
        UV::fromGp0(gp0Command.index(4), c, p, *this),
        UV::fromGp0(gp0Command.index(6), c, p, *this),
        UV::fromGp0(gp0Command.index(8), c, p, *this),
    };
    
    renderer->pushQuad(positions, {}, uvs, curAttribute);
}

void Emulator::Gpu::gp0ImageLoad(uint32_t val) {
    uint32_t cords = gp0Command.index(1);
    uint32_t res = gp0Command.index(2);
    
    /*startX = curX = (cords & 0xFFFF) & 0x3FF;
    startY = curY = ((cords & 0xFFFF0000) >> 16) & 0x1FF;
    
    // 2nd  Source Coord      (YyyyXxxxh) ; write to GP0 port (as usual?)
    endX = startX + (((res & 0xFFFF) - 1) & 0x3FF) + 1;
    endY = startY + ((((res & 0xFFFF0000) >> 16) - 1) & 0x1FF) + 1;*/
    
    uint32_t x =  cords        & 0x3FF;
    uint32_t y = (cords >> 16) & 0x1FF;
    
    uint32_t w = (((res & 0x3FF) - 1) & 0x3FF) + 1;
    uint32_t h = ((((res >> 16) & 0x1FF) - 1) & 0x1FF) + 1;
    
    startX = startX24 = curX = x;
    startY = startY24 = curY = y;
    
    if (displayDepth == DisplayDepth::D24Bits) {
        startX24 = (startX24 * 2) / 3;
        
        uint32_t widthInPixels = (w * 2) / 3;
        endX24 = startX24 + widthInPixels;
        endX = startX + w;
        
        vram->halfword_count = 0;
    } else {
        endX = startX + w;
    }
    
    endY = startY + h;
    
    gp0Mode = VRam;
}

void Emulator::Gpu::gp0ImageStore(uint32_t val) {
    uint32_t cords = gp0Command.index(1); // this is supposed to be 3 ;-; Huh???
    uint32_t res = gp0Command.index(2);
    
    /*uint32_t x =  cords        & 0x3FF;
    uint32_t y = (cords >> 16) & 0x1FF;
    
    uint32_t w =  res        & 0x3FF;
    uint32_t h = (res >> 16) & 0x1FF;
    
    startX = curX = x;
    startY = curY = y;
    
    endX = x + w;
    endY = y + h;
    
    readMode = VRam;*/
    
    uint32_t x = (cords & 0xFFFF) & 0x3FF;
    uint32_t y = ((cords & 0xFFFF0000) >> 16) & 0x1FF;
    
    uint32_t width = (((res & 0xFFFF) - 1) & 0x3FF) + 1;
    uint32_t height = ((((res & 0xFFFF0000) >> 16) - 1) & 0x1FF) + 1;
    
    startX = startX24 = curX = x;
    startY = startY24 = curY = y;
    
    // TODO; Handle 24bit
    endX = startX + width;
    endY = startY + height;
    
    renderer->readbackToVram(startX, startY, width, height);
    readMode = VRam;
}

void Emulator::Gpu::gp0VramToVram(uint32_t val) const {
    uint32_t cords = gp0Command.index(1);
    uint32_t dests = gp0Command.index(2);
    uint32_t res = gp0Command.index(3);
    
    /*uint32_t srcX =  cords        & 0x3FF;
    uint32_t srcY = (cords >> 16) & 0x1FF;
    
    uint32_t dstX =  dests        & 0x3FF;
    uint32_t dstY = (dests >> 16) & 0x1FF;
    
    uint32_t w =  res        & 0x3FF;
    uint32_t h = (res >> 16) & 0x1FF;
    
    int stepX = 1;
    int stepY = 1;
    
    if (dstX > srcX) stepX = -1;
    if (dstY > srcY) stepY = -1;
    
    int startX = (stepX > 0) ? 0 : w - 1;
    int startY = (stepY > 0) ? 0 : h - 1;
    
    for (int y = startY; y >= 0 && y < (int)h; y += stepY){
        for (int x = startX; x >= 0 && x < (int)w; x += stepX){
            uint16_t c = vram->getPixel(srcX + x, srcY + y);
            vram->writePixel(dstX + x, dstY + y, c);
        }
    }*/
    
    uint32_t srcX = (cords & 0xFFFF) & 0x3FF;
    uint32_t srcY = ((cords & 0xFFFF0000) >> 16) & 0x1FF;
    
    uint32_t dstX = (dests & 0xFFFF) & 0x3FF;
    uint32_t dstY = ((dests & 0xFFFF0000) >> 16) & 0x1FF;
    
    uint32_t width = (((res & 0xFFFF) - 1) & 0x3FF) + 1;
    uint32_t height = ((((res & 0xFFFF0000) >> 16) - 1) & 0x1FF) + 1;
    
    bool dir = srcX < dstX;
    
    for(uint32_t y = 0; y < height; y++) {
        for(uint32_t x = 0; x < width; x++) {
            uint32_t posX = (!dir) ? x : width - 1 - x;
            
            uint16_t color = vram->getPixel((srcX + posX), (srcY + y));
            vram->writePixel(dstX + posX, dstY + y, color);
        }
    }
    
    if (!renderVRamToScreen) return;
    
    // TODO; VERIFY THIS
    uint32_t x = dstX;
    uint32_t y = dstY;
    uint32_t w = width;
    uint32_t h = height;
    
    vram->flushRegion(x, y, w, h);
    
    uint32_t glY = (512 - y) - h;
    
    renderer->flushDrawCommands();
    vram->copyToTexture(x, glY, x, glY, w, h, renderer->sceneTex[renderer->curTex]);
}

void Emulator::Gpu::gp1(uint32_t val) {
    uint32_t opcode = (val >> 24) & 0xFF;
    
    switch (opcode) {
        case 0x00:
            gp1Reset(val);
            break;
        case 0x01:
            gp1ResetCommandBuffer(val);
            break;
        case 0x02:
            gp1AcknowledgeIrq(val);
            break;
        case 0x03:
            gp1DisplayEnable(val);
            break;
        case 0x04:
            gp1DmaDirection(val);
            break;
        case 0x05:
            gp1DisplayVramStart(val);
            break;
        case 0x06:
            gp1DisplayHorizontalRange(val);
            break;
        case 0x07:
            gp1DisplayVerticalRange(val);
            break;
        case 0x08:
            gp1DisplayMode(val);
            break;
        case 0x09: {
            // GP1(09h) - New Texture Disable
            //   0     Texture Disable (0=Normal, 1=Allow Disable via GP0(E1h).11) ;GPUSTAT.15
            newTextureDisable = val & 1;
            break;
        }
        // -> 1F
        case 0x10: {
            readMode = Command;
            
            /**
             * On New 208pin GPUs, following values can be selected:
             * 00h-01h = Returns Nothing (old value in GPUREAD remains unchanged)
             * 02h     = Read Texture Window setting  ;GP0(E2h) ;20bit/MSBs=Nothing
             * 03h     = Read Draw area top left      ;GP0(E3h) ;20bit/MSBs=Nothing
             * 04h     = Read Draw area bottom right  ;GP0(E4h) ;20bit/MSBs=Nothing
             * 05h     = Read Draw offset             ;GP0(E5h) ;22bit
             * 06h     = Returns Nothing (old value in GPUREAD remains unchanged)
             * 07h     = Read GPU Type (usually 2)    ;see "GPU Versions" chapter
             * 08h     = Unknown (Returns 00000000h) (lightgun on some GPUs?)
             * 09h-0Fh = Returns Nothing (old value in GPUREAD remains unchanged)
             * 10h-FFFFFFh = Mirrors of 00h..0Fh
             */
            
            switch (val & 0xF) {
                // Returns nothing
                case 0:
                case 1:
                    break;
                case 2: {
                    _read = (textureWindowYOffset << 15) | (textureWindowXOffset << 10) | (textureWindowYMask << 5) | textureWindowXMask;
                    
                    break;
                }
                case 3: {
                    _read = (drawingAreaTop << 10) | drawingAreaLeft;
                    
                    break;
                }
                case 4: {
                    _read = (drawingAreaBottom << 10) | drawingAreaRight;
                    
                    break;
                }
                case 5: {
                    _read = (((drawingYOffset) & 0x7FF) << 11) | ((drawingXOffset) & 0x7FF);
                    
                    break;
                }
                case 6: {
                    // R  06h     = Returns Nothing (old value in GPUREAD remains unchanged)
                    
                    break;
                }
                case 7: {
                    _read = 2; // GPU VERSION
                    
                    break;
                }
                case 8: {
                    _read = 0;
                    
                    break;
                }
                case 9:
                case 10:
                case 11:
                case 12:
                case 13:
                case 14:
                case 15:
                {
                    // It says nothing..? Should it reset last read?
                    break;
                }
                default: {
                    printf("Unknown GP1 read value %d\n", (val & 0xF));
                    std::cerr << "";
                    assert(false);
                    
                    break;
                }
            }
            
            break;
        }
        case 0x20: {
            // Do nothing
            break;
        }
        default:
            std::cerr << "ERROR; Unhandled GPU1 command " << std::to_string(opcode) << '\n';
            assert(false);
            
            break;
    }
}

void Emulator::Gpu::gp1Reset(uint32_t val) {
    interrupt = false;
    
    pageBaseX = 0;
    pageBaseY = 0;
    semiTransparency = 0;
    textureDepth = TextureDepth::T4Bit;
    textureWindowXMask = 0;
    textureWindowYMask = 0;
    textureWindowXOffset = 0;
    textureWindowYOffset = 0;
    dithering = false;
    drawToDisplay = false;
    textureDisable = false;
    displayHorizFlip = false;
    rectangleTextureFlipX = false;
    rectangleTextureFlipY = false;
    drawingAreaLeft = 0;
    drawingAreaTop = 0;
    drawingAreaRight = 0;
    drawingAreaBottom = 0;
    drawingXOffset = 0;
    drawingYOffset = 0;
    forceSetMaskBit = false;
    preserveMaskedPixels = false;
    
    _read = 0;
    
    dmaDirection = DmaDirection::Off;
    
    displayEnabled = false;
    displayVramXStart = 0;
    displayVramYStart = 0;
    hres = HorizontalRes::fromFields(0, 0);
    vres = VerticalRes::Y240Lines;
    
    vmode = VMode::Ntsc;
    interlaced = false;
    displayHorizStart = 0x200;
    displayHorizEnd = 0x200 + 256 * 10;
    displayLineStart = 0x10;
    displayLineEnd = 0x10 + 240;
    displayDepth = DisplayDepth::D15Bits;
    
    // XXX should also clear the command FIFO when we implement it
    // XXX Should also invalidate GPU chance if we ever implement it
    gp0Command.clear();
    gp0CommandRemaining = 0;
    gp0Mode = Command;
    readMode = Command;
}

void Emulator::Gpu::gp1DisplayMode(uint32_t val) {
    // https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#gp108h-display-mode
    
    uint8_t hr1 = static_cast<uint8_t>(val & 3);
    uint8_t hr2 = static_cast<uint8_t>((val >> 6) & 1);
    
    // Resolutions: 256, 320, 512, 640, 368
    hres = HorizontalRes::fromFields(hr1, hr2);
    
    vres = (val & 0x4) != 0 ? VerticalRes::Y480Lines : VerticalRes::Y240Lines;

    VMode mode = (val & 0x8) != 0 ? VMode::Pal : VMode::Ntsc;

    if (mode != vmode) {
        _gpuFrac = 0;
        printf("GPU Switched display mode to: %s\n", (val & 0x8) != 0 ? "PAL" : "NTSC");
    }

    vmode = mode;
    // vmode = (val & 0x8) != 0 ? VMode::Pal : VMode::Ntsc;

    displayDepth = (val & 0x10) != 0 ? DisplayDepth::D24Bits : DisplayDepth::D15Bits;
    
    interlaced = (val & 0x20) != 0;
    
    // (or, always 1 when GP1(08h).5=0)
    field = static_cast<Field>(interlaced);
    
    displayHorizFlip = (val & 0x80) != 0;
}

void Emulator::Gpu::gp1DisplayEnable(uint32_t val) {
    displayEnabled = (val & 1) == 0;
    
    if (!displayEnabled)
        printf("AUP?\n");
    
    //assert(displayEnabled);
}

void Emulator::Gpu::gp1DisplayVramStart(uint32_t val) {
    uint32_t rawX =  val        & 0x3FF; // bits 0–9  (0..1023, halfword offset)
    uint32_t rawY = (val >> 10) & 0x1FF; // bits 10–18 (0..511 VRAM lines)
    
    uint32_t x = (rawX & ~7);   // align down to 8 pixels
    uint32_t y = rawY;
    
    displayVramXStart = x;
    displayVramYStart = y;
}

void Emulator::Gpu::gp1DmaDirection(uint32_t val) {
    switch (val & 3) {
        case 0:
            dmaDirection = DmaDirection::Off;
            break;
        case 1:
            dmaDirection = DmaDirection::Fifo;
            break;
        case 2:
            dmaDirection = DmaDirection::CpuToGp0;
            break;
        case 3:
            dmaDirection = DmaDirection::VRamToCpu;
            break;
        default:
            dmaDirection = DmaDirection::Off;
            
            throw std::runtime_error("This shouldn't happen.. GP1 Direction; " + std::to_string(val & 3));
    }
}

void Emulator::Gpu::gp1DisplayHorizontalRange(uint32_t val) {
    /*displayHorizStart = static_cast<uint16_t>(val & 0x3FF);
    displayHorizEnd = static_cast<uint16_t>((val >> 12) & 0x3FF);*/

    displayHorizStart = static_cast<uint16_t>(val & 0x0FFF);
    displayHorizEnd = static_cast<uint16_t>((val >> 12) & 0x0FFF);

    /**
     * The number of displayed pixels per line is "(((X2-X1)/cycles_per_pix)+2) AND NOT 3"
     * (ie. the hardware is rounding the width up/down to a multiple of 4 pixels).
     */
    uint32_t range = displayHorizEnd - displayHorizStart;

    switch (hres.getResolution()) {
        case 256: cyclesPerPixel = 10; break;
        case 320: cyclesPerPixel = 8;  break;
        case 368: cyclesPerPixel = 7;  break;
        case 512: cyclesPerPixel = 5;  break;
        case 640: cyclesPerPixel = 4;  break;
    }

    displayedPixels = (((range / cyclesPerPixel) + 2) & ~3);
}

void Emulator::Gpu::gp1DisplayVerticalRange(uint32_t val) {
    displayLineStart = static_cast<uint16_t>(val & 0x3FF);
    displayLineEnd = static_cast<uint16_t>((val >> 10) & 0x3FF);
}

void Emulator::Gpu::gp1ResetCommandBuffer(uint32_t val) {
    gp0Command.clear();
    gp0CommandRemaining = 0;
    gp0Mode = Command;
    readMode = Command;
}

void Emulator::Gpu::gp1AcknowledgeIrq(uint32_t val) {
    if (!interrupt)
        return;
    
    interrupt = false;
}

uint32_t Emulator::Gpu::read() {
    if(readMode == Command)
        return _read;
    
    const auto step = [&]() {
        if (++curX >= endX) {
            curX = startX;
            
            if (++curY >= endY) {
                readMode = Command;
            }
        }
    };
    
    uint32_t data = 0;
    
    data |= vram->getPixel(curX, curY);
    step();
    data |= vram->getPixel(curX, curY) << 16;
    step();
    
    return data;
}

void Emulator::Gpu::reset() {
    pageBaseX = 0;
    pageBaseY = 0;
    semiTransparency = 0;
    textureDepth = TextureDepth::T4Bit;
    dithering = false;
    drawToDisplay = true;
    forceSetMaskBit = false;
    preserveMaskedPixels = false;
    field = Field::Top;
    textureDisable = false;
    hres = HorizontalRes::fromFields(1, 0); // 320 default
    vres = VerticalRes::Y240Lines;
    vmode = VMode::Ntsc;
    displayDepth = DisplayDepth::D15Bits;
    interlaced = false;
    displayEnabled = false;
    interrupt = false;
    dmaDirection = DmaDirection::Off;
    displayHorizFlip = false;
    rectangleTextureFlipX = false;
    rectangleTextureFlipY = false;
    
    textureWindowXMask = 0;
    textureWindowYMask = 0;
    textureWindowXOffset = 0;
    textureWindowYOffset = 0;
    
    drawingAreaLeft = 0;
    drawingAreaTop = 0;
    drawingAreaRight = 0;
    drawingAreaBottom = 0;
    drawingXOffset = 0;
    drawingYOffset = 0;
    
    displayVramXStart = 0;
    displayVramYStart = 0;
    displayHorizStart = 0x200;
    displayHorizEnd = 0x200 + 256 * 10;
    displayLineStart = 0x10;
    displayLineEnd = 0x10 + 240;
    
    _read = 0;
    _scanLine = 0;
    _cycles = 0;
    lastGpuCycles = 0;
    _gpuTimerFrac = 0;
    _gpuFrac = 0;
    _hpos = 0;
    frames = 0;

    lastCmd = 0;
    lastOp = 0;
    prvOps.clear();
    fields.clear();

    updateDotClock();
    isInHBlank = calcHBlank();
    isInVBlank = calcVBlank();
    isOddLine = false;

    gp0Command.clear();
    gp0CommandRemaining = 0;
    gp0Mode = Command;
    readMode = Command;
    Gp0CommandMethod = nullptr;
    
    curAttribute = {};
    
    // TODO; ?
    //if (vram)
    //    vram->reset();
    
    // TODO; ?
    // if (renderer)
        //renderer->reset();
}

void Emulator::Gpu::setTextureDepth(TextureDepth depth) {
    this->textureDepth = depth;
}
