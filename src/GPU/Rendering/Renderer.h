#pragma once

#include "Buffer.h"
#include "Rasterizer.h"

#include "glm/vec2.hpp"

/*#ifdef _WIN32
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#endif*/

class GLFWwindow;

namespace Emulator {
    class Renderer {
        public:
            explicit Renderer(Emulator::Gpu& gpu);
            
            //void init();
            void display(bool displayEntireScreen = false);
            void renderFrame();
            void flushDrawCommands();
            void readbackToVram(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
            
            void clear();
            
        private:
            void draw();
            uint32_t vidx(uint32_t i) const;
            
        public:
            void pushLine(Emulator::Gpu::Position positions[], Emulator::Gpu::Color colors[], Emulator::Gpu::UV uvs[], Emulator::Gpu::Attributes attributes);
            void pushTriangle(Emulator::Gpu::Position positions[], Emulator::Gpu::Color colors[], Emulator::Gpu::UV uvs[], Emulator::Gpu::Attributes attributes);
            void pushQuad(Emulator::Gpu::Position positions[], Emulator::Gpu::Color colors[], Emulator::Gpu::UV uvs[], Emulator::Gpu::Attributes attributes);
            void pushRectangle(Emulator::Gpu::Position positions[], Emulator::Gpu::Color colors[], Emulator::Gpu::UV uvs[], Emulator::Gpu::Attributes attributes);
            
        public:
            void setDrawingArea(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom) const;
            void setTextureWindow(uint8_t textureWindowXMask, uint8_t textureWindowYMask, uint8_t textureWindowXOffset, uint8_t textureWindowYOffset);
            void setSemiTransparencyMode(uint8_t semiTransparencyMode) const;
            
            void bindFrameBuffer(GLuint buf);
            
            static GLuint compileShader(const char* source, GLenum shaderType);
            GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);
            GLuint getProgramAttrib(GLuint program, const std::string& attr);
            
            static std::string getShaderSource(const std::string& path);
            
            GLuint createFrameBuffer(GLsizei width, GLsizei height, GLuint& textureId);
            
        private:
            // GLFW callbacks
            /*static void APIENTRY openglDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
                                                GLsizei length, const GLchar *message, const void *userParam);*/
            
            void setupScreenQuad();
            void setPrimitiveMode(GLenum mode);

        public:
            bool renderVRAM = false;
            bool cropToDisplayArea = true;
            
            // Shader parameters
            GLuint vertexShader;
            GLuint fragmentShader;
            
            GLuint program;
            
            bool upscalingEnabled = true;
            
            // Vertex Array Object
            GLuint VAO;
            
            GLuint sceneFBO[2];
            GLuint sceneTex[2];
            int curTex = 0;
            GLuint bloomFBO[2];
            
            //GLuint sceneTexture;
            GLuint bloomTexture[2];
            GLuint quadVAO;
            GLuint quadVBO;
            
            // Bloom
            GLuint bloomThresholdProgram;
            GLuint blurProgram;
            
            GLuint postProcessProgram;
            GLuint postProcessVertexShader;
            GLuint postProcessFragmentShader;
            
            // Uniforms
            GLint drawingMinUni;
            GLint drawingMaxUni;
            GLint textureWindowUni;
            GLint semiTransparencyModeUni;
            GLint ditheringUni;
            GLint maskBitUni;
            GLint checkMaskUni;

            // Buffer contains the vertices positions
            Buffer<Gpu::Position> positions;
            
            // Buffer contains the vertices colors
            Buffer<Gpu::Color> colors;
            
            // Buffer contains the texture coordinates
            Buffer<Gpu::UV> uvs;
            
            // Buffer contains the vertices attributes
            Buffer<Gpu::Attributes> attributes;
            
            // Current number of vertices in the buffers
            uint32_t nVertices;
            uint32_t drawCalls = 0;

            GLsync regionFences[BUFFER_REGIONS] = {};
            uint32_t bufferRegion = 0;

            GLFWwindow* window;
        public:
            // Bloom
            float threshold = 0.65f;
            float blurRadius = 1.5f;
            int bloomPasses = 1;
            
            float kernelB = 0.0f;
            float kernelC = 0.5f;
            float sharpness = 1.0f;
            float gamma = 0.8f;
            float scanline = 0.01f;
            float bloomIntensity = 1.2f;
            float lodBias = 0.5f;
            float edgeThreshold = 0.1f;
            float ditherStrength = 0.005f;
            float noiseStrength = 0.01f;
            float contrast = 1.1f;
            float saturation = 1.2f;
            float halation = 0.15f;
            
            int sampleRadius = 2;
            
            bool enableAdaptiveSharpening = true;
            bool enableBloom = false;
            bool enableUpscaling = false;
            bool useShaders  = false;

        private:
            bool horizontal = true;
            
            glm::vec2 drawingArea;

            GLenum primitiveMode = GL_TRIANGLES;
            GLint pixelCenterModeUni;

        private:
            Emulator::Gpu& gpu;
            
            Rasterizer _rasterizer;
    };
}
