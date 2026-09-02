#version 330 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

uniform vec2 uUvMin;
uniform vec2 uUvMax;

void main() {
    vUV = mix(uUvMin, uUvMax, aUV);
    gl_Position = vec4(aPos, 0.0, 1.0);
}
