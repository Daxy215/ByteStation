#version 420 core

in vec3 color;

//uniform ivec2 drawingAreaMin;
//uniform ivec2 drawingAreaMax;
flat in ivec2 drawingAreaMinIn;
flat in ivec2 drawingAreaMaxIn;
in vec2 VRAMPos;

/*
 * Because I'm too lazy,
 * I decided to add clut position,
 * to this, so, x, y are the uvs,
 * and z and w are the clut positions.
 * 
 * Because I'm even more lazy,
 * I split the data into a single float,
 * so:
 * UVs.z, has clutX AND pageX
 * UVs.w, has clutY AND pageY
 */
in vec4 UVs;

/**
 * 0 = isSemiTransparent
 * 1 = blendTexture
 * 2 = useTextures ( 0 = No texture, 1 = Only texture, 2 = Texture + Color)
 */
flat in int attr;

out vec4 fragColor;

layout(binding = 0) uniform sampler2D texture_sample4;
layout(binding = 1) uniform sampler2D sceneTex;

//uniform int texture_depth;
uniform vec4 textureWindow;
uniform int semiTransparencyMode;
uniform int dithering;
uniform int setMaskBit;
uniform int checkMaskBit;

const int IS_SEMITRANSPARENT_MASK = 0x1;
const int BLEND_TEXTURE_MASK = 0x2;
const int TEXTURE_MODE_MASK = 0x1C;
const int TEXTURE_MODE_SHIFT = 2;
const int TEXTURE_DEPTH_SHIFT = 5;
const int TEXTURE_DEPTH_MASK = 0x3;
const int SEMI_TRANSPARENCY_MODE_SHIFT = 7;
const int SEMI_TRANSPARENCY_MODE_MASK = 0x3;

vec2 calculateTexel() {
    uvec2 texel = uvec2(uint(UVs.x) % 256u, uint(UVs.y) % 256u);

    uvec2 mask = uvec2(textureWindow.x, textureWindow.y);
    uvec2 offset = uvec2(textureWindow.z, textureWindow.w);

    texel.x = (texel.x & ~(mask.x * 8u)) | ((offset.x & mask.x) * 8u);
    texel.y = (texel.y & ~(mask.y * 8u)) | ((offset.y & mask.y) * 8u);

    return vec2(texel);
}

uint internalToPsxColor(vec4 c) {
    uint a = uint(floor(c.a + 0.5));
    uint r = uint(floor(c.r * 31.0 + 0.5));
    uint g = uint(floor(c.g * 31.0 + 0.5));
    uint b = uint(floor(c.b * 31.0 + 0.5));

    return (a << 15) | (b << 10) | (g << 5) | r;
}

vec3 psxColor5(vec3 c) {
    return floor(floor(clamp(c, 0.0, 1.0) * 255.0 + 0.5) / 8.0);
}

int psxDitherOffset(ivec2 p) {
    int x = p.x & 3;
    int y = p.y & 3;

    if (y == 0) {
        if (x == 0) return -4;
        if (x == 1) return 0;
        if (x == 2) return -3;
        return 1;
    }
    if (y == 1) {
        if (x == 0) return 2;
        if (x == 1) return -2;
        if (x == 2) return 3;
        return -1;
    }
    if (y == 2) {
        if (x == 0) return -3;
        if (x == 1) return 1;
        if (x == 2) return -4;
        return 0;
    }

    if (x == 0) return 3;
    if (x == 1) return -1;
    if (x == 2) return 2;
    return -2;
}

vec3 psxDither24To15(vec3 c, ivec2 p) {
    if (dithering == 0) {
        return c;
    }

    vec3 color255 = floor(clamp(c, 0.0, 1.0) * 255.0 + 0.5);
    color255 += vec3(psxDitherOffset(p));
    color255 = clamp(color255, 0.0, 255.0);

    return floor(color255 / 8.0) / 31.0;
}

vec3 psxColorFrom5(vec3 c) {
    return clamp(c, 0.0, 31.0) / 31.0;
}

vec3 psxSemiBlend(vec3 back, vec3 front, int mode) {
    vec3 b = psxColor5(back);
    vec3 f = psxColor5(front);
    vec3 o;

    if (mode == 0) {
        o = floor((b + f) * 0.5);
    } else if (mode == 1) {
        o = b + f;
    } else if (mode == 2) {
        o = b - f;
    } else {
        o = b + floor(f * 0.25);
    }

    return psxColorFrom5(o);
}

vec4 read(int x, int y) {
    y = 512 - 1 - y;
    
    return texelFetch(texture_sample4, ivec2(x, y), 0);
}

vec4 clut4bit(vec2 coords, ivec2 clut, ivec2 page) {
    int texX = int(coords.x / 4.0) + page.x;
    int texY = int(coords.y)       + page.y;

    uint index = internalToPsxColor(read(texX, texY));
    uint which = (index >> ((uint(coords.x) & 3u) * 4u)) & 0xfu;

    return read(clut.x + int(which), clut.y);
}

vec4 clut8bit(vec2 coords, ivec2 clut, ivec2 page) {
    int texX =  int(coords.x / 2.0) + page.x;
    int texY =  int(coords.y)       + page.y;

    uint index = internalToPsxColor(read(texX, texY));
    uint which = (index >> ((uint(coords.x) & 1u) * 8u)) & 0xffu;

    return read(int(which) + clut.x, clut.y);
}

vec4 clut16bit(vec2 coords, ivec2 page) {
    return read(int(coords.x) + page.x, int(coords.y) + page.y);
}

vec4 sample_texel(int textureDepth) {
    /*
     * Extract clut and page
     */
    float clutX = int(UVs.z) >> 16;
    float clutY = int(UVs.w) >> 16;

    float pageX = int(UVs.z) & 0xFFFF;
    float pageY = int(UVs.w) & 0xFFFF;

    vec2 texel = calculateTexel();

    /**
     * T4Bit = 0,
     * T8Bit = 1,
     * T15Bit = 2,
     */
    if (textureDepth == 0) {
        return clut4bit(texel, ivec2(clutX, clutY), ivec2(pageX, pageY));
    } else if(textureDepth == 1) {
        return clut8bit(texel, ivec2(clutX, clutY), ivec2(pageX, pageY));
    } else if(textureDepth == 2) {
        return clut16bit(texel, ivec2(pageX, pageY));
    }

    return vec4(1, 0, 0, 1);
}

void main() {
    //int y = int(VRAMPos.y);
    //fragColor = read(int(VRAMPos.x), y);
    
    //return;
    
    // Crop
    if (any(lessThan(VRAMPos, drawingAreaMinIn)) ||
        any(greaterThanEqual(VRAMPos, drawingAreaMaxIn))) {
        discard;
    }

    if (checkMaskBit != 0) {
        int existingY = 511 - int(VRAMPos.y);
        vec4 existing = texelFetch(sceneTex, ivec2(int(VRAMPos.x), existingY), 0);

        if (existing.a >= 0.5) {
            discard;
        }
    }

    /**
     * 0 = isSemiTransparent
     * 1 = blendTexture
     * 2 = textureMode ( 0 = No texture, 1 = Only texture, 2 = Texture + Color)
     */
    int isSemiTransparent = int(attr) & IS_SEMITRANSPARENT_MASK;
    int blendTexture = (int(attr) & BLEND_TEXTURE_MASK) >> 1;
    int textureMode = (int(attr) & TEXTURE_MODE_MASK) >> TEXTURE_MODE_SHIFT;
    int semiMode = (int(attr) >> SEMI_TRANSPARENCY_MODE_SHIFT) & SEMI_TRANSPARENCY_MODE_MASK;
    
    // Apply transparency
    vec4 outColor, samp, F;
    bool texelSemiTransparent = false;
    
    // textureMode ( 0 = No texture, 1 = Only texture, 2 = Texture + Color)
    if(textureMode == 1 || textureMode == 2) {
        int textureDepth = (attr >> TEXTURE_DEPTH_SHIFT) & TEXTURE_DEPTH_MASK;
        samp = sample_texel(textureDepth);
        uint psxTexel = internalToPsxColor(samp);
        
        // If black just ignore
        if(psxTexel == 0u) {
            discard;
        }
        
        texelSemiTransparent = (psxTexel & 0x8000u) != 0u;
    }
    
    // textureMode ( 0 = No texture, 1 = Only texture, 2 = Texture + Color)
    if (textureMode == 0) {
        if (blendTexture == 1) {
            // Texel = 128 (0x80) because TME=0
            vec3 col255 = color * 255.0;
            vec3 tex255 = vec3(128.0);
            
            vec3 blended255 = floor((tex255 * col255) / 128.0);
            F.rgb = blended255 / 255.0;
            F.a = 1.0;
        } else {
            F = vec4(color, 1.0);
        }
    } else {
        // Textured primitives
        if (textureMode == 1) {
            F = samp;
        } else { // textureMode == 2
            F = samp * vec4(color, 1.0);
        }
        
        if (blendTexture == 1) {
            // finalChannel.rgb = (texel.rgb * vertexColour.rgb) / vec3(128.0)
            vec3 tex5 = samp.rgb * 31.0;
            vec3 tex255 = tex5 * (255.0 / 31.0);
            vec3 col255 = color * 255.0;
            
            vec3 blended255 = floor((tex255 * col255) / 128.0);
            
            F.rgb = blended255 / 255.0;
            F.a   = samp.a;
        }
    }
    
    /*
     * B=Back  (the old pixel read from the image in the frame buffer)
     * F=Front (the new halftransparent pixel)
     * 0.5 x B + 0.5 x F    ;aka B/2+F/2
     * 1.0 x B + 1.0 x F    ;aka B+F
     * 1.0 x B - 1.0 x F    ;aka B-F
     * 1.0 x B +0.25 x F    ;aka B+F/4
     */
    bool shouldBlend = isSemiTransparent == 1 && (textureMode == 0 || texelSemiTransparent);
    if(shouldBlend) {
        int y = 511 - int(VRAMPos.y);
        vec4 B = texelFetch(sceneTex, ivec2(int(VRAMPos.x), y), 0);
        outColor = vec4(psxSemiBlend(B.rgb, psxDither24To15(F.rgb, ivec2(VRAMPos)), semiMode), 1.0);
    } else {
        // No transparency
        outColor = vec4(psxDither24To15(F.rgb, ivec2(VRAMPos)), F.a);
    }
    
    fragColor = vec4(outColor.rgb, (setMaskBit != 0 || texelSemiTransparent) ? 1.0 : 0.0);
}
