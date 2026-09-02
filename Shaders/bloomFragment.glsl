#version 330 core

in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D screenTexture;
uniform sampler2D bloomTexture;

uniform vec2 uTextureSize;

uniform vec2 uOutputSize;

uniform float uKernelB;         // B-spline parameter
uniform float uKernelC;         // Cubic parameter
uniform float uSharpness;       // Sharpness strength
uniform bool uEnableUpscaling = false;

uniform int uSampleRadius = 2;                 // Kernel radius
uniform float uLODBias = 0.5;                  // Mipmap LOD bias
uniform float uEdgeThreshold = 0.1;            // Edge detection threshold
uniform float uDitherStrength = 0.005;         // Dithering strength
uniform float uNoiseStrength = 0.01;           // Film grain strength
uniform float uContrast = 1.1;                 // Contrast adjustment
uniform float uSaturation = 1.2;               // Color saturation
uniform float uHalation = 0.15;                // CRT halation effect
uniform float uTime = 0;                       // Just a timer
uniform bool uEnableAdaptiveSharpening = true; // Edge-aware sharpening

// Bloom parameters
uniform float uBloomIntensity = 1.2;
uniform bool uEnableBloom = true;

// CRT parameters
uniform float uScanline = 0.3;
uniform float uGamma = 2.2;

float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898,78.233))) * 43758.5453);
}

vec3 applyDither(vec3 color, vec2 uv) {
    float noise = random(uv * uOutputSize) - 0.5;
    return color + noise * uDitherStrength;
}

vec3 applyFilmGrain(vec3 color, vec2 uv) {
    float noise = random(uv + vec2(uTime));
    return color * (1.0 - uNoiseStrength) + noise * uNoiseStrength;
}

vec3 adjustContrastSaturation(vec3 color, float contrast, float saturation) {
    vec3 luminance = vec3(dot(color, vec3(0.299, 0.587, 0.114)));
    vec3 chroma = color - luminance;
    return luminance + chroma * saturation * contrast;
}

// Bicubic weight
float cubicWeight(float x, float B, float C) {
    float ax = abs(x);
    float ax2 = ax * ax;
    float ax3 = ax * ax2;
    
    return (ax < 1.0) ?
    ((12.0 - 9.0*B - 6.0*C)*ax3 +
    (-18.0 + 12.0*B + 6.0*C)*ax2 +
    (6.0 - 2.0*B)) / 6.0 :
    (ax < 2.0) ?
    ((-B - 6.0*C)*ax3 +
    (6.0*B + 30.0*C)*ax2 +
    (-12.0*B - 48.0*C)*ax +
    (8.0*B + 24.0*C)) / 6.0 :
    0.0;
}

vec4 textureCubic(sampler2D tex, vec2 uv, vec2 texSize) {
    vec2 coord = uv * texSize - 0.5;
    vec2 base = floor(coord);
    vec2 f = coord - base;
    
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    
    for(int y = -uSampleRadius; y <= uSampleRadius; y++) {
        for(int x = -uSampleRadius; x <= uSampleRadius; x++) {
            vec2 offset = vec2(x, y);
            float wx = cubicWeight(offset.x - f.x, uKernelB, uKernelC);
            float wy = cubicWeight(offset.y - f.y, uKernelB, uKernelC);
            float weight = wx * wy;
            
            vec2 samplePos = (base + offset + 0.5) / texSize;
            sum += textureLod(tex, samplePos, uLODBias) * weight;
            wsum += weight;
        }
    }
    
    return sum / max(wsum, 0.0001);
}

vec3 sharpen(vec3 color, vec3 blurred, float strength, vec2 uv) {
    if(uEnableAdaptiveSharpening) {
        vec3 edge = abs(color - blurred);
        float edgeStrength = smoothstep(uEdgeThreshold, uEdgeThreshold * 2.0, length(edge));
        strength *= mix(0.5, 1.5, edgeStrength);
    }
    
    vec3 sharpened = color + (color - blurred) * strength;
    
    return clamp(sharpened, 0.0, 1.0);
}

vec3 applyBloom(vec3 color, vec2 uv) {
    vec3 bloom = texture(bloomTexture, uv).rgb * uBloomIntensity;
    
    return color + bloom;
}

vec3 crtEffects(vec3 color, vec2 uv) {
    // Halation effect
    vec3 blurred = textureLod(bloomTexture, uv, 2.0).rgb;
    color = mix(color, blurred, uHalation);

    // Scanlines with phosphor simulation
    vec2 fragCoord = uv * uTextureSize * 2.0;
    float scanline = mix(1.0, abs(sin(fragCoord.y * 3.141592 * 1.5)), uScanline);
    
    return color * scanline;
}

vec3 applyGamma(vec3 color) {
    return pow(color, vec3(1.0 / uGamma));
}

void main() {
    if(!uEnableUpscaling) {
        FragColor = texture(screenTexture, TexCoords);
        return;
    }
    
    // Upscale with quality-controlled bicubic
    vec4 upscaled = textureCubic(screenTexture, TexCoords, uTextureSize);
    vec3 color = upscaled.rgb;
    
    // Adaptive sharpening
    vec3 blurred = textureLod(screenTexture, TexCoords, 1.0 + uLODBias).rgb;
    color = sharpen(color, blurred, uSharpness, TexCoords);
    
    // Color adjustments
    color = adjustContrastSaturation(color, uContrast, uSaturation);
    
    // Post-processing effects
    if(uEnableBloom)
        color = applyBloom(color, TexCoords);
    
    color = crtEffects(color, TexCoords);
    color = applyDither(color, TexCoords);
    color = applyFilmGrain(color, TexCoords);
    color = applyGamma(color);
    
    FragColor = vec4(color, 1.0);
}