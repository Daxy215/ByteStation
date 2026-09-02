#version 330 core

in vec2 vertexPosition;
in uvec3 vertexColor;
in vec4 texCoords;

// Idk how to pass structs?
// Not really good with GLSL
in int attributes;

out vec3 color;
out vec4 UVs;

// Not sure how to forward data,
// to the Fragment shader right away..
//flat out uvec3 attr;
flat out int attr;
flat out ivec2 drawingAreaMinIn;
flat out ivec2 drawingAreaMaxIn;
//flat out uint textureAttr[4];

uniform ivec2 drawingAreaMin;
uniform ivec2 drawingAreaMax;
uniform int pixelCenterMode;

out vec2 VRAMPos;

const int TEXTURE_MODE_MASK = 0x1C;
const int TEXTURE_MODE_SHIFT = 2;

void main() {
    float x1 = (drawingAreaMin.x);
    float y1 = (drawingAreaMin.y);
    float x2 = (drawingAreaMax.x);
    float y2 = (drawingAreaMax.y);

    vec2 position = vertexPosition;

    if (pixelCenterMode != 0) {
        position += vec2(0.5, 0.5);
    }

    //vec2 position = vec2(x1, y1) + vertexPosition;
    vec2 lPos = position/* - vec2(drawingAreaMin)*/;
    
    //ivec2 area = ivec2(drawingAreaMax - drawingAreaMin);
    //ivec2 area = ivec2(x2, y2);
    ivec2 area = ivec2(1024, 512);
    
    VRAMPos = lPos;
    drawingAreaMinIn = drawingAreaMin;
    drawingAreaMaxIn = drawingAreaMax;
    
    // For testing
    /*uint isSemiTransparent = attributes & IS_SEMITRANSPARENT_MASK;
    uint blendTexture = (attributes & BLEND_TEXTURE_MASK) >> 1;*/
    int textureMode = (int(attributes) & TEXTURE_MODE_MASK) >> TEXTURE_MODE_SHIFT;
    
    /*
    * Converts VRAM coordinates (0; area.x, 0; area.y)
    * to OpenGL coordinates (-1;1, -1,1)
    */
    float xPos = (VRAMPos.x / float(area.x)) * 2.0 - 1.0;
    
    // VRAM puts 0 at the top, OpenGL at the bottom..;
    float yPos = 1.0 - (VRAMPos.y / float(area.y)) * 2.0;
    
    // Set the final position
    gl_Position = vec4(xPos, yPos, 0.0, 1.0);
    
    // Convert the components from [0;255] to [0;1]
    color = vec3(float(vertexColor.r) / 255.0,
                 float(vertexColor.g) / 255.0,
                 float(vertexColor.b) / 255.0);
    
    UVs = texCoords;
    attr = attributes;
}
