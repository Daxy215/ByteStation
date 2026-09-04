#pragma once

#ifndef GPU_H
#define GPU_H

#include <functional>
#include <stdexcept>
#include <string>
#include <GL/glew.h>

#include "VRAM.h"

// TODO; Please redo this shitty code WTF IS THIS ;-;

// https://psx-spx.consoledev.net/graphicsprocessingunitgpu/

namespace Emulator {
    class Renderer;
    class VRAM;
}

namespace Emulator {
    // Depth of the pixel values in a texture page
    enum class TextureDepth {
        T4Bit = 0,
        T8Bit = 1,
        T15Bit = 2,
    };
    
    // Interlaced output splits each frame in two fields
    enum class Field {
        Top = 0,
        Bottom = 1,
    };
    
    // Video output horizontal resolution
    struct HorizontalRes {
        uint32_t value;
        
        // (0=256, 1=320, 2=512, 3=640)
        uint32_t hr1;
        
        // (0=256/320/512/640, 1=368)
        uint32_t hr2;
        
        static HorizontalRes fromFields(uint32_t hr1, uint32_t hr2) {
            uint32_t hr = (hr2 & 1) | ((hr1 & 3) << 1);
            return HorizontalRes{hr, hr1, hr2};
        }
        
        uint32_t intoStatus() const {
            auto f = static_cast<uint32_t>(value) << 16;
            return f;
        }
        
        uint32_t getResolution() const {
            if (hr2 == 1) return 368;
            if (hr1 == 0) return 256;
            if (hr1 == 1) return 320;
            if (hr1 == 2) return 512;
            if (hr1 == 3) return 640;
            return hr1;
        }
    };
    
    // Video output vertical resolution
    enum class VerticalRes {
        Y240Lines = 0,
        Y480Lines = 1,
    };
    
    // Video Modes
    enum class VMode {
        Ntsc = 0,
        Pal = 1,
    };
    
    // Display area color depth
    enum class DisplayDepth {
        D15Bits = 0,
        D24Bits = 1,
    };
    
    // Requested DMA direction
    enum class DmaDirection {
        Off = 0,
        Fifo = 1,
        CpuToGp0 = 2,
        VRamToCpu = 3,
    };
    
    // Buffer holding multi-word fixed-length GP0 command parameters
    struct CommandBuffer {
        // Command buffer: the longest possible command is GP0(0x3E)
        // which takes 12 parameters
        uint32_t buffer[12];
        
        // Number of words queued in buffer
        uint8_t len;
        
        CommandBuffer() : buffer(), len(0) {}
        
        [[nodiscard]] uint32_t index(size_t index) const {
            if (index >= len) {
                throw std::out_of_range("Command buffer index out of range: " + std::to_string(index));
            }
            
            return buffer[index];
        }
        
        void clear() {
            len = 0;
        }
        
        void pushWord(uint32_t word) {
            buffer[static_cast<size_t>(len)] = word;
            len++;
        }
    };
    
    enum Gp0Mode {
        // Default mode: Handling commands
        Command,
        // Loading an image into VRAM
        VRam,
        // To render poly-lines
        PolyLine,
        
        // Uhh
        Polygon,
        Line,
        Rectangle,
        
        // VRAM
        VRamToVRam,
        CpuToVRam,
        VRamToCpu,
    };

    class Gpu {
        static constexpr uint32_t NTSC_TICKS_PER_LINE = 3413;
        static constexpr uint32_t NTSC_TOTAL_LINES = 263;
        static constexpr uint32_t NTSC_HORIZONTAL_ACTIVE_START = 488;
        static constexpr uint32_t NTSC_HORIZONTAL_ACTIVE_END = 3288;
        static constexpr uint32_t NTSC_VERTICAL_ACTIVE_START = 16;
        static constexpr uint32_t NTSC_VERTICAL_ACTIVE_END = 256;

        static constexpr uint32_t PAL_TICKS_PER_LINE = 3406;
        static constexpr uint32_t PAL_TOTAL_LINES = 314;
        static constexpr uint32_t PAL_HORIZONTAL_ACTIVE_START = 488;
        static constexpr uint32_t PAL_HORIZONTAL_ACTIVE_END = 3300;
        static constexpr uint32_t PAL_VERTICAL_ACTIVE_START = 20;
        static constexpr uint32_t PAL_VERTICAL_ACTIVE_END = 308;

        static constexpr uint64_t NTSC_CRTC_NUM = 715909; // 715909, 11
        static constexpr uint64_t NTSC_CRTC_DEN = 451584; // 451584, 7

        static constexpr uint64_t PAL_CRTC_NUM = 709379;
        static constexpr uint64_t PAL_CRTC_DEN = 451584;

        public:
            // TODO; Change this back to GLint
            struct Position {
                Position() = default;
                
                Position(int x, int y) : x(x), y(y) {}
                Position(uint32_t x, uint32_t y) : x(x), y(y) {}
                Position(float x, float y) : x(x), y(y) {}
                
                static Position fromGp0(uint32_t val) {
                    int16_t x = (int16_t)((val & 0x7FF) << 5) >> 5;
                    int16_t y = (int16_t)(((val >> 16) & 0x7FF) << 5) >> 5;
                    
                    float fx = float(x + Gpu::drawingXOffset);
                    float fy = float(y + Gpu::drawingYOffset);
                    
                    return { fx, fy };
                    
                    /*float x = static_cast<float>(static_cast<int16_t>(val));
                    float y = static_cast<float>(static_cast<int16_t>(val >> 16));
                    
                    x += Gpu::drawingXOffset;
                    y += Gpu::drawingYOffset;
                    
                    return {x, y};*/
                }
                
                float x, y;
            };
            
            struct Color {
                Color() = default;
                
                // GPU BGR555 color
                Color(uint16_t p) {
                    r = ((p >> 10) & 0x1F) << 3;
                    g = ((p >> 5)  & 0x1F) << 3;
                    b = ( p        & 0x1F) << 3;
                }
                
                Color(GLubyte r, GLubyte g, GLubyte b) : r(r), g(g), b(b) {
                    
                }
                
                static Color fromGp0(uint32_t val) {
                    uint8_t r = static_cast<uint8_t>(val);
                    uint8_t g = static_cast<uint8_t>(val >> 8);
                    uint8_t b = static_cast<uint8_t>(val >> 16);
                    
                    return {r, g, b};
                }
                
                uint32_t toU32() const {
                    uint32_t color = 0;
                    
                    color |= (r & 0xF8) >> 3;
                    color |= (g & 0xF8) << 2;
                    color |= (b & 0xF8) << 7;
                    
                    return color;
                }
                
                GLubyte r, g, b;
            };
            
            struct UV {
                UV() = default;
                UV(float u, float v) : u(u), v(v) {}
                UV(float u, float v, float dataX, float dataY) : u(u), v(v), dataX(dataX), dataY(dataY) {}
                
                static UV fromGp0(uint32_t val, uint32_t clut, uint32_t page, Gpu& gpu) {
                    //uint16_t depth = (gpu.textureDepth == TextureDepth::T4Bit) ? 4 : (gpu.textureDepth == TextureDepth::T8Bit) ? 8 : 16;
                    //assert(depth == 4);
                    
                    /**
                     * 0 -> 4 bits
                     * 1 -> 8 bits
                     * 2 -> 16 bits
                     * 3 -> 16 bits?
                     */
                    int depth = ((page & 0x180) >> 7);
                    gpu.setTextureDepth(static_cast<Emulator::TextureDepth>(depth));

                    // Was causing text not to show for mortal kombat
                    gpu.pageBaseX = static_cast<uint8_t>(page & 0xF);
                    gpu.pageBaseY = static_cast<uint8_t>((page >> 4) & 1);
                    gpu.semiTransparency = static_cast<uint8_t>((page >> 5) & 3);
                    gpu.curAttribute.setTextureDepth(depth);
                    
                    float u = static_cast<float>((val) & 0xFF);
                    float v = static_cast<float>((val >> 8) & 0xFF);
                    
                    uint16_t clutX = static_cast<uint16_t>((clut & 0x3F) << 4);
                    uint16_t clutY = static_cast<uint16_t>((clut >> 6) & 0x1FF);
                    
                    uint16_t pageX = static_cast<uint16_t>((page & 0xF) << 6);
                    uint16_t pageY = static_cast<uint16_t>(((page >> 4) & 1) << 8);
                    
                    /*float r = 16 / depth;
                    float ux = (pageX * r + u) / (1024.0f * r);
                    float vc = (pageY + v) / 512.0f;*/
                    
                    float dataX = (clutX << 16) | (pageX);
                    float dataY = (clutY << 16) | (pageY);
                    
                    return {(u), v, dataX, dataY};
                    //return {ux, vc, dataX, dataY};
                }
                
                static UV fromGp0(uint32_t val, uint32_t clut, uint16_t pageX, uint16_t pageY, Gpu& gpu) {
                    //uint16_t depth = (gpu.textureDepth == TextureDepth::T4Bit) ? 4 : (gpu.textureDepth == TextureDepth::T8Bit) ? 8 : 16;
                    //assert(depth == 4);
                    
                    pageX *= 64;
                    pageY *= 256;
                    
                    float u = static_cast<uint16_t>((val) & 0xFF);
                    float v = static_cast<uint16_t>((val >> 8) & 0xFF);
                    
                    uint16_t clutX = static_cast<uint16_t>((clut & 0x3F) << 4);
                    uint16_t clutY = static_cast<uint16_t>((clut >> 6) & 0x1FF);
                    
                    float dataX = (clutX << 16) | pageX;
                    float dataY = (clutY << 16) | pageY;
                    
                    return {u, v, dataX, dataY};
                }
                
                float u = 0, v = 0;
                float dataX, dataY;
            };

            /**
             * Make this use uint32_t,
             * and pass the "offset" variables, as well as the,
             * textureDepth to fix the flickering issue(I hope so)
             *
             * huh?
             *
             * Fixed: Just had bad rendering timings..
             */
            enum TextureMode : uint8_t {
                ColorOnly    = 0,
                TextureOnly  = 1,
                TextureColor = 2
            };
            
            struct Attributes {
                uint32_t _reg = 0;
                
                Attributes() = default;
                Attributes(uint32_t reg) : _reg(reg) {}
                
                Attributes(int isSemiTransparent, int blendTexture, TextureMode textureMode = ColorOnly) {
                    setSemiTransparent(isSemiTransparent);
                    setBlendTexture(blendTexture);
                    setTextureMode(textureMode);
                }
                
                void setSemiTransparent(int v) {
                    if (v) _reg |= 0x1u;
                    else   _reg &= ~0x1u;
                }

                void setBlendTexture(int v) {
                    if (v) _reg |= 0x2u;
                    else   _reg &= ~0x2u;
                }
                
                void setTextureMode(TextureMode mode) {
                    _reg = (_reg & ~(0x7u << 2)) | ((uint32_t(mode) & 0x7u) << 2);
                }
                
                void setTextureDepth(int d) {
                    _reg = (_reg & ~(0x3u << 5)) | ((uint32_t(d) & 0x3u) << 5);
                }
                
                void setSemiTransparencyMode(int d) {
                    _reg = (_reg & ~(0x3u << 7)) | ((uint32_t(d) & 0x3u) << 7);
                }

                int isSemiTransparent() const { return (_reg >> 0) & 0x1; }
                int blendTexture()      const { return (_reg >> 1) & 0x1; }
                TextureMode textureMode() const { return TextureMode((_reg >> 2) & 0x7u); }
                int textureDepth()      const { return (_reg >> 5) & 0x3; }
                int semiTransparencyMode() const { return (_reg >> 7) & 0x3; }
                
                bool usesColor()   const { return textureMode() != TextureOnly; }
                bool useTextures() const { return textureMode() != ColorOnly; }
            };
            
            struct BitField {
                const char* name;
                uint8_t start;
                uint8_t length;
            };
            
            /*union Polygon {
                struct {
                    uint32_t rgb               : 24; //    23-0        rgb    first color value.
                    uint32_t isRawTexture      : 1 ; //    24          1/0    raw texture / modulation
                    uint32_t isSemiTransparent : 1 ; //    25          1/0    semi-transparent / opaque
                    uint32_t isTextured        : 1 ; //    26          1/0    textured / untextured
                    uint32_t isFourVerts       : 1 ; //    27          1/0    4 / 3 vertices
                    uint32_t isGouraud         : 1 ; //    28          1/0    gouraud / flat shading
                    uint32_t _                 : 3 ; //    31-29       001    polygon render
                } __attribute__((packed));
                
                uint32_t reg = 0;
                
                explicit Polygon(uint32_t reg) : reg(reg) {}
            };*/
            
        public:
            Gpu(bool enableRendering = true);
            
            bool step(uint32_t cycles);
            bool stepCRTC(uint32_t ticks);
            uint32_t ticksUntilNextCRTCEvent() const;
            uint32_t cpuCyclesUntilNextCRTCEvent() const;

            uint32_t dotClockDivider() const {
                switch (hres.getResolution()) {
                    case 256: return 10;
                    case 320: return 8;
                    case 368: return 7;
                    case 512: return 5;
                    case 640: return 4;
                    default:  return 8;
                }
            }

            uint32_t currentDot() const {
                return _hpos / dotClockDivider();
            }

            void updateDotClock() {
            }

            void advanceDotClock(uint32_t ticks) {
                /*if (!timers.usesDotClock(0))
                    return;*/

                uint32_t before = currentDot();
                uint32_t after = (_hpos + ticks) / dotClockDivider();

                //if (after > before)
                //    timers.addTicks(0, after - before);
            }

            /**
             * Duckstation uses those
             * can't find where from..
             *
             * PAL:  3406 video cycles per scanline (or 3406.1 or so?)
             * NTSC: 3413 video cycles per scanline (or 3413.6 or so?)
             *
             * ig?
             * NTSC: 3288 - 488 = 2800 ticks / 53.693175MHz = 52.15us
             * PAL:  3300 - 488 = 2812 ticks / 53.203425MHz = 52.85us
             */
            uint32_t hblankStart() const {
                return vmode == VMode::Pal ? PAL_HORIZONTAL_ACTIVE_END : NTSC_HORIZONTAL_ACTIVE_END;
            }

            /**
             * Duckstation uses those
             * can't find where from..
             */
            uint32_t hblankEnd() const {
                return vmode == VMode::Pal ? PAL_HORIZONTAL_ACTIVE_START : NTSC_HORIZONTAL_ACTIVE_START;
            }

            bool calcHBlank() const {
                return _hpos < hblankEnd() || _hpos >= hblankStart();
            }

            bool calcVBlank() const {
                uint32_t start = std::min<uint32_t>(displayLineStart, vtotal());
                uint32_t end = std::min<uint32_t>(displayLineEnd, vtotal());

                return _scanLine < start || _scanLine >= end;
            }

            uint32_t htotal() const {
                return vmode == VMode::Pal ? PAL_TICKS_PER_LINE : NTSC_TICKS_PER_LINE;
            }

            uint32_t vtotal() const {
                return vmode == VMode::Pal ? PAL_TOTAL_LINES : NTSC_TOTAL_LINES;
            }

            void onHBlankStart() {
                //timers.setGate(0, true);

                //if (timers.usesHBlankClock(1))
                //    timers.addTicks(1, 1);
            }

            void onHBlankEnd() {
                //timers.setGate(0, false);
            }

            void onVBlankStart() {
                //timers.setGate(1, true);
                //interrupt.raise(Interrupt::VBlank);
            }

            void onVBlankEnd() {
                //timers.setGate(1, false);
            }

            void updateOddLineBit() {
                if (interlaced && vres == VerticalRes::Y480Lines) {
                    isOddLine = (displayVramYStart + (isInVBlank ? 0 : (displayField ? 1 : 0))) & 1;
                    return;
                }

                 isOddLine = (displayVramYStart + _scanLine) & 1;
                //if (i/sInVBlank) {
                    //isOddLine = false;
                    //return;
                //}
                
                //isOddLine = (_scanLine + displayVramYStart) & 1;
                
                /*if (interlaced && vres == VerticalRes::Y480Lines)
                    isOddLine = frames & 1;
                else
                    isOddLine = (_scanLine + displayVramYStart) & 1;*/
            }

            uint32_t status();
            
            // Handles writes to the GP0 command register
            void gp0(uint32_t val);
            
            // GP0(0xE1): command
            void gp0DrawMode(uint32_t val);
            
            // GP0(0XE3): Set Drawing Area top left
            void gp0DrawingAreaTopLeft(uint32_t val);
            
            // GP0(0xE4): Set Drawing Area bottom right
            void gp0DrawingAreaBottomRight(uint32_t val);
            
            // GP0(0xE5): Set Drawing Offset
            void gp0DrawingOffset(uint32_t val) const;
            
            // GP0(0xE2): Set Texture Window
            void gp0TextureWindow(uint32_t val);
            
            // GP0(0xE6): Set Mask Bit Setting
            void gp0MaskBitSetting(uint32_t val);
            
            // GP0(0x00): No operations
            void gp0Nop(uint32_t val) { /*NOP*/ }
            
            // GP0(0x28): Momochrome Opaque Quadrilateral
            void gp0QuadMonoOpaque(uint32_t val);
            
            // TODO; Remove
            void vertexOrderTri(Position p[3]) {
                auto crossZ = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x);
                
                if(crossZ < 0) {
                    std::swap(p[0], p[1]);
                }
            }
            
            inline float cross2D(const Position& a, const Position& b, const Position& c) {
                return (b.x - a.x) * (c.y - a.y) -
                    (b.y - a.y) * (c.x - a.x);
            }
            
            void vertexOrderRect(Position p[4], Color c[4], UV u[4]) {
                float crossZ = cross2D(p[0], p[1], p[2]);
                
                // If clockwise, swap vertices 1 and 3
                if (crossZ < 0.0f) {
                    std::swap(p[1], p[3]);
                    std::swap(c[1], c[3]);
                    std::swap(u[1], u[3]);
                }
            }
            
            // GP0(0x30): Shaded Opaque Triangle
            void gp0TriangleShadedOpaque(uint32_t val);
            
            // GP0(0x34): Shaded Texture Opaque Triangle
            void gp0TriangleTexturedShadedOpaque(uint32_t val);
            
            // GP0(0x38): gp0QuadShadedOpaque
            void gp0QuadShadedOpaque(uint32_t val);
            
            // GP0(0x3C): Shaded Texture Opaque Quad
            void gp0QuadTexturedShadedOpaque(uint32_t val);
            
            //  GP0(40h) - Monochrome line, opaque
            //  GP0(42h) - Monochrome line, semi-transparent
            void gp0MonoLine(uint32_t val);
            
            // GP0(48h): Monochrome Poly-line, opaque
            void gp0PolyLineMono(uint32_t val);
            
            // GP0(52h) - Shaded line, semi-transparent
            void gp0ShadedLine(uint32_t val);
            
            //  GP0(58h) - Shaded Poly-line, opaque
            void gp0ShadedPolyLine(uint32_t val);
            
            // Helper function
            void renderRectangle(Position position, Color color, UV uv, uint16_t width, uint16_t height);
            
            // y tf do these say "MonoOpaque"???????
            void gp0VarRectangleMonoOpaque(uint32_t val);
            void gp0VarTexturedRectangleMonoOpaque(uint32_t val);
            void gp0DotRectangleMonoOpaque(uint32_t val);
            void gp08RectangleMonoOpaque(uint32_t val);
            void gp08RectangleTexturedOpaqu(uint32_t val);
            void gp016RectangleMonoOpaque(uint32_t val);
            void gp016RectangleTextured(uint32_t val);
            
            // GP0(0x01): Clear Cache
            void gp0ClearCache(uint32_t val);
            
            // GP0(0x02): Full VRam
            void gp0FillVRam(uint32_t val);
            
            // GP0(0x20): 
            void gp0TriangleMonoOpaque(uint32_t val);
            
            void gp0TriangleTexturedOpaque(uint32_t val);
            
            void gp0TriangleRawTexturedOpaque(uint32_t val);
            
            // GP0(0x2C): Quad Raw Texture Blend Opqaue
            void gp0QuadTextureBlendOpaque(uint32_t val);
            
            // GP0(0x2D): Quad Raw Texture Blend Opqaue
            void gp0QuadRawTextureBlendOpaque(uint32_t val);
            
            // GP0(0xA0): Load Image
            // From CPU to VRAM
            void gp0ImageLoad(uint32_t val);
            
            // GP0(0xC0): Load Store
            // From VRAM to CPU
            void gp0ImageStore(uint32_t val);
            
            void gp0VramToVram(uint32_t val) const;
            
            // Handles writes to the GP1 command register
            void gp1(uint32_t val);
            
            // GP1(0x00): soft rest
            void gp1Reset(uint32_t val);
            
            // GP1(0x80): Display Mode
            void gp1DisplayMode(uint32_t val);
            
            // GP1(0x03): Display Enable
            void gp1DisplayEnable(uint32_t val);
            
            // GP1(0x04): DMA Direction
            void gp1DmaDirection(uint32_t val);
            
            // GP1(0x05): Display VRAM Start
            void gp1DisplayVramStart(uint32_t val);
            
            // GP1(0x06): Display Horizontal Range
            void gp1DisplayHorizontalRange(uint32_t val);
            
            // GP1(0x07): Display Vertical Range
            void gp1DisplayVerticalRange(uint32_t val);
            
            // GP1(0x01): Reset Command Buffer
            void gp1ResetCommandBuffer(uint32_t val);
            
            // GP1(0x02): Acknowledge Interrupt
            void gp1AcknowledgeIrq(uint32_t val);
            
            // Retrieve value of the "read" register
            uint32_t read();
            
            void reset();
            
        private:
            [[nodiscard]] bool readyToReceiveCommandWord() const;
            [[nodiscard]] bool readyToSendVramToCpu() const;
            [[nodiscard]] bool readyToReceiveDmaBlock() const;
            
            void setTextureDepth(TextureDepth depth);
            
            uint32_t extract(uint32_t reg, uint8_t start, uint8_t len) {
                uint32_t mask = ((1u << len) - 1u);
                return (reg >> start) & mask;
            }
            
            std::unordered_map<std::string, uint32_t> decodeFields(uint32_t reg, const BitField* fields, size_t count) {
                std::unordered_map<std::string, uint32_t> out;
                
                for (size_t i = 0; i < count; i++)
                    out[fields[i].name] = extract(reg, fields[i].start, fields[i].length);
                
                return out;
            }

            bool areTexturesDisabled() {
                return textureDisable & newTextureDisable;
            }
            
        public:
            uint8_t pageBaseX;          // Texture page base X coordinate (4 bits, 64 byte increment)
            uint8_t pageBaseY;          // Texture page base Y coordinate (1 bit, 256 line increment)
            uint8_t semiTransparency;   // Semi-transparency value
            TextureDepth textureDepth;  // Texture page color depth
            bool dithering;             // Enable dithering from 24 to 15 bits RGB
            bool drawToDisplay;         // Allow drawing to the display area
            bool forceSetMaskBit;       // Force "mask" bit of the pixel to 1 when writing to VRAM
            bool preserveMaskedPixels;  // Don't draw to pixels which have the "mask" bit set
            Field field;                // Currently displayed field
            bool textureDisable;        // When true all textures are disabled
            bool newTextureDisable;     // When true all textures are disabled (GP1)
            HorizontalRes hres;         // Video output horizontal resolution
            VerticalRes vres;           // Video output vertical resolution
            VMode vmode;                // Video mode
            DisplayDepth displayDepth;  // Display depth
            bool interlaced;            // Output interlaced video signal instead of progressive
            bool displayEnabled;        // Whether the display is on or not
            bool interrupt;             // True when the interrupt is active
            DmaDirection dmaDirection;  // DMA request direction
            bool displayHorizFlip;      // Horizontal display flip flag (GPUSTAT.14 on v1 GPUs)
            bool rectangleTextureFlipX; // Mirror textured rectangles along the x axis
            bool rectangleTextureFlipY; // Mirror textured rectangles along the Y axis
            
            uint8_t textureWindowXMask;   // Texture window x mask (8 pixel steps)
            uint8_t textureWindowYMask;   // Texture window y mask (8 pixel steps)
            uint8_t textureWindowXOffset; // Texture window x offset (8 pixel steps)
            uint8_t textureWindowYOffset; // Texture window y offset (8 pixel steps)
            int16_t drawingAreaLeft;     // Left-most column of drawing area
            int16_t drawingAreaTop;      // Top-most line of drawing area
            int16_t drawingAreaRight;    // Right-most column of drawing area
            int16_t drawingAreaBottom;   // Bottom-most line of drawing area
            static inline int16_t drawingXOffset;       // Horizontal drawing offset applied to all vertices
            static inline int16_t drawingYOffset;       // Vertical drawing offset applied to all vertices
            uint32_t displayVramXStart;   // First column of the display area in VRAM
            uint32_t displayVramYStart;   // First line of the display area in VRAM
            uint16_t displayHorizStart;   // Display output horizontal start relative to HSYNC
            uint16_t displayHorizEnd;     // Display output horizontal end relative to HSYNC
            uint16_t displayLineStart;    // Display output first line relative to VSYNC
            uint16_t displayLineEnd;      // Display output last line relative to VSYNC
            
            bool renderVRamToScreen = true;
            
        public:
            int startX, startY, curX, curY, endX, endY, endX24;
            
        public:
            // TODO; TESTING
            int startX24;
            int startY24;
            
        private:
            uint32_t _read = 0;
            
            //const float ntscVideoClock = 53693175.0f / 60.0f;
            //const float palVideoClock = 53203425.0f / 60.0f;
		    
        public:
            uint32_t _scanLine = 0;
            uint64_t _cycles = 0;
            uint32_t lastGpuCycles = 0;
            uint64_t _gpuTimerFrac = 0;
            uint64_t _gpuFrac = 0;
            uint32_t _hpos = 0;
            uint32_t frames = 0;
            uint32_t cyclesPerPixel = 0;
            uint32_t displayedPixels = 0;
            uint32_t lastDotTicks = 0;
            uint32_t dotFrac = 0;

        public:
            bool isInHBlank = false;
            bool isInVBlank = false;
            bool displayField = false;
            
        private:
            bool isOddLine = false;
            
            // 10, 8, 5, 4, 7
            uint32_t dotCycles[5] = { 10, 8, 5, 4, 7};
            
        private:
            Attributes curAttribute = {};
            
        public:
            // Buffer containing the current GP0 command
            CommandBuffer gp0Command;
            
            // Remaining words for the current GP0 command
            uint32_t gp0CommandRemaining = 0;
            
            // Current mode for GP0
            Gp0Mode gp0Mode = Command;
            Gp0Mode readMode = Command;
            
            // Pointer to the method implementing the current GP command
            std::function<void(Gpu&, uint32_t)> Gp0CommandMethod;
            
        public:
            Renderer* renderer;
            VRAM* vram;
    };
}
#endif // GPU_H
