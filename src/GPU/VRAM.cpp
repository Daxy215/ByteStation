#include "VRAM.h"
#include "Gpu.h"
#include "Rendering/Renderer.h"

#include <iostream>

Emulator::VRAM::VRAM(Gpu &gpu) : gpu(gpu) {
    tilesX = MAX_WIDTH / TILE_SIZE;
    tilesY = MAX_HEIGHT / TILE_SIZE;
    tileDirty.resize(tilesX * tilesY);
    
    size15 = MAX_WIDTH * MAX_HEIGHT * sizeof(uint16_t);
    size24 = MAX_WIDTH * MAX_HEIGHT * sizeof(uint32_t);
    
    glCreateTextures(GL_TEXTURE_2D, 1, &tex15);
    glTextureStorage2D(tex15, 1, GL_RGBA8, MAX_WIDTH, MAX_HEIGHT);
    
    glTextureParameteri(tex15, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(tex15, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(tex15, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex15, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // 15-bit PBO
    glCreateBuffers(1, &pbo15);
    glNamedBufferStorage(
        pbo15,
        size15,
        nullptr,
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT
    );
    gpu15 = static_cast<uint16_t*>(glMapNamedBufferRange(
        pbo15,
        0,
        size15,
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT
    ));
    
    glCreateTextures(GL_TEXTURE_2D, 1, &tex24);
    glTextureStorage2D(tex24, 1, GL_RGBA8, MAX_WIDTH, MAX_HEIGHT);
    
    glTextureParameteri(tex24, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(tex24, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(tex24, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex24, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glCreateBuffers(1, &pbo24);
    glNamedBufferStorage(
        pbo24,
        size24,
        nullptr,
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT
    );
    gpu24 = static_cast<uint32_t*>(glMapNamedBufferRange(
        pbo24,
        0,
        size24,
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT
    ));

    glGenFramebuffers(1, &blitSrcFBO);
    glGenFramebuffers(1, &blitDstFBO);

    GLenum err = glGetError();
    if(err != GL_NO_ERROR) {
        std::cerr << "OpenGL Error during Renderer constructor: " << err << '\n';
    }

    reset();
}

Emulator::VRAM::~VRAM() {
    glDeleteTextures(1, &tex15);
    glDeleteBuffers(1, &pbo15);
    glDeleteBuffers(1, &pbo24);
}

void Emulator::VRAM::endTransfer() {
    glPixelStorei(GL_UNPACK_ROW_LENGTH, MAX_WIDTH);
    
    for (int ty = 0; ty < tilesY; ty++) {
        for (int tx = 0; tx < tilesX; tx++) {
            const int i = ty * tilesX + tx;
            
            if (!tileDirty[i])
                continue;
            
            tileDirty[i] = 0;
            
            const int x = tx * TILE_SIZE;
            const int y = ty * TILE_SIZE;
            
            if (gpu.displayDepth == DisplayDepth::D24Bits) {
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo24);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                
                const size_t offset = (y * MAX_WIDTH + x) * sizeof(uint32_t);
                
                glTextureSubImage2D(tex24, 0, x, y, TILE_SIZE, TILE_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, reinterpret_cast<void *>(offset));
            } else {
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo15);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                
                glTextureSubImage2D(tex15, 0, x, y, TILE_SIZE, TILE_SIZE, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV,
                                    reinterpret_cast<void *>((y * MAX_WIDTH + x) * sizeof(uint16_t)));
            }
        }
    }
    
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    //glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void Emulator::VRAM::writePixel(uint32_t x, uint32_t y, const uint16_t pixel) {
    const uint16_t pixel0 = (pixel & 0xFFFF);
    
    x &= (MAX_WIDTH - 1);
    y &= (MAX_HEIGHT - 1);
    
    if (gpu.preserveMaskedPixels && (getPixel(x, y) & 0x8000)) {
        return;
    }
    
    const uint16_t mask = (gpu.forceSetMaskBit << 15);
    setPixel(x, y, pixel0 | mask);
}

// Test
uint8_t fifo[6];
int fifo_len = 0;

void Emulator::VRAM::setPixel(uint32_t x, uint32_t y, uint16_t color) {
    // TODO; Double check this
    x %= MAX_WIDTH;
    y %= MAX_HEIGHT;

    // https://stackoverflow.com/questions/73092064/is-there-a-way-to-convert-16-bit-color-to-24-bit-color-efficiently-while-avoidin
    uint32_t initalX = ((gpu.startX * 2) / 3);
    
    if (gpu.displayDepth == DisplayDepth::D24Bits) {
        if (gpu.startX24 >= MAX_WIDTH || gpu.startY24 >= MAX_HEIGHT) {
            //printf("BAD PIXEL x=%u y=%u\n", x, y);
            return;
        }
        
        if (halfword_count == 0) {
            hw0 = color;
            halfword_count = 1;
        } else if (halfword_count == 1) {
            hw1 = color;
            halfword_count = 2;
        } else {
            uint16_t hw2 = color;
            
            uint8_t r0 =  hw0        & 0xFF;
            uint8_t g0 = (hw0 >> 8)  & 0xFF;
            uint8_t b0 =  hw1        & 0xFF;
            
            uint8_t r1 = (hw1 >> 8) & 0xFF;
            uint8_t g1 =  hw2       & 0xFF;
            uint8_t b1 = (hw2 >> 8) & 0xFF;
            
            uint32_t y0 = MAX_HEIGHT - gpu.startY24 - 1;
            
            gpu24[y0 * MAX_WIDTH + gpu.startX24] = (0xFFu << 24) | (b0 << 16) | (g0 << 8) | r0;
            markTile(gpu.startX24, y0);
            gpu.startX24++;
            
            if (gpu.startX24 >= gpu.endX24) {
                gpu.startX24 = initalX;
                gpu.startY24++;
            }
            
            uint32_t y1 = MAX_HEIGHT - gpu.startY24 - 1;
            
            gpu24[y1 * MAX_WIDTH + gpu.startX24] = (0xFFu << 24) | (b1 << 16) | (g1 << 8) | r1;
            markTile(gpu.startX24, y1);
            gpu.startX24++;
            
            if (gpu.startX24 >= gpu.endX24) {
                gpu.startX24 = initalX;
                gpu.startY24++;
            }
            
            halfword_count = 0;
        }
    } else {
        if (x >= MAX_WIDTH || y >= MAX_HEIGHT) {
            //printf("BAD PIXEL x=%u y=%u\n", x, y);
        }
        
        y = MAX_HEIGHT - y - 1;
        
        gpu15[y * MAX_WIDTH + x] = color;
        markTile(x, y);
    }
}

uint16_t Emulator::VRAM::getPixel(uint32_t x, uint32_t y) const {
    x %= MAX_WIDTH;
    y %= MAX_HEIGHT;
    
    y = MAX_HEIGHT - y - 1;
    
    if (gpu.displayDepth == DisplayDepth::D24Bits)
        return gpu24[y * MAX_WIDTH + x];
    
    return gpu15[y * MAX_WIDTH + x];
}

uint16_t Emulator::VRAM::getPixel4(uint32_t x, uint32_t y, uint32_t clutX, uint32_t clutY, uint32_t pageX,
                                   uint32_t pageY) {
    uint16_t texel = getPixel(pageX + x / 4, pageY + y);
    uint32_t index = (texel >> (x % 4) * 4) & 0xF;
    
    return getPixel(clutX + index, clutY);
}

uint16_t Emulator::VRAM::getPixel8(uint32_t x, uint32_t y, uint32_t clutX, uint32_t clutY, uint32_t pageX,
                                   uint32_t pageY) {
    uint16_t texel = getPixel(pageX + x / 2, pageY + y);
    uint32_t index = (texel >> (x % 2) * 8) & 0xFF;
    
    return getPixel(clutX + index, clutY);
}

uint16_t Emulator::VRAM::getPixel16(uint32_t x, uint32_t y, uint32_t pageX, uint32_t pageY) {
    return getPixel(x + pageX, y + pageY);
}

uint16_t Emulator::VRAM::RGB555_to_RGB565(uint16_t color) {
    uint16_t red   = (color & 0x7C00) >> 10;
    uint16_t green = (color & 0x03E0) >> 5;
    uint16_t blue  = color & 0x001F;
    
    uint16_t red565   = red;
    uint16_t green565 = (green << 1);
    uint16_t blue565  = blue;
    
    return (red565 << 11) | (green565 << 5) | blue565;
}

void Emulator::VRAM::copyToTexture(uint32_t x, uint32_t y, uint32_t dx, uint32_t dy, uint32_t width, uint32_t height, GLuint tex) {
    auto curTex = getCurrentTexture();
    uint32_t dstScale = gpu.renderer->internalScale;

    if (dstScale <= 1) {
        glCopyImageSubData(
            curTex, GL_TEXTURE_2D, 0,
            x, y, 0,
            tex, GL_TEXTURE_2D, 0,
            dx, dy, 0,
            width, height, 1
        );

        return;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, blitSrcFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, curTex, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blitDstFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    glBlitFramebuffer(
        x, y, x + width, y + height,
        dx * dstScale, dy * dstScale, (dx + width) * dstScale, (dy + height) * dstScale,
        GL_COLOR_BUFFER_BIT, GL_NEAREST
    );

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int Emulator::VRAM::getCurrentTexture() {
    auto curTex = tex15;
    if (gpu.displayDepth == DisplayDepth::D24Bits)
        curTex = tex24;
    
    GLint isTex = 0;
    glBindTexture(GL_TEXTURE_2D, curTex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &isTex);
    if(isTex == 0) {
        std::cerr << "ERROR: Current texture " << curTex << " is invalid!" << std::endl;
        return 0;
    }
    
    return curTex;
}

void Emulator::VRAM::reset() {
    std::fill_n(gpu24, MAX_WIDTH * MAX_HEIGHT, 0/*0xFFFFFFFF*/);
    std::fill_n(gpu15, MAX_WIDTH * MAX_HEIGHT, 0/*0x7FFF*/);
    
    //std::fill(gpu24, gpu24 + MAX_WIDTH * MAX_HEIGHT, 0);
    //std::fill(gpu15, gpu15 + MAX_WIDTH * MAX_HEIGHT, 0);
    
    //std::fill(tileDirty.begin(), tileDirty.end(), 1);
}

void Emulator::VRAM::markTile(uint32_t x, uint32_t y) {
    uint32_t tx                 = x / TILE_SIZE;
    uint32_t ty                 = y / TILE_SIZE;
    tileDirty[ty * tilesX + tx] = 1;
}
