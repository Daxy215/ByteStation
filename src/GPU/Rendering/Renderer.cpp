#include "Renderer.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

#include <GLFW/glfw3.h>
#include <glm/detail/func_geometric.inl>

#include "imgui.h"

#include "../Gpu.h"

// #define Test
//  Ik I shouldn't do this but im lazy
enum { WIDTH = 1024, HEIGHT = 512 };

/*namespace {
    static int orient2d(const Emulator::Gpu::Position &a, const Emulator::Gpu::Position &b, int x, int y) {
        return int((b.x - a.x) * (float(y) - a.y) - (b.y - a.y) * (float(x) - a.x));
    }

    static int area2d(const Emulator::Gpu::Position &a, const Emulator::Gpu::Position &b,
                      const Emulator::Gpu::Position &c) {
        return int((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
    }

    static bool isTopLeft(const Emulator::Gpu::Position &a, const Emulator::Gpu::Position &b) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;

        return dy < 0.0f || (dy == 0.0f && dx > 0.0f);
    }

    static bool insideTriangle(const Emulator::Gpu::Position v[3], int x, int y) {
        const int area = area2d(v[0], v[1], v[2]);

        if (area == 0) {
            return false;
        }

        const int sign = area > 0 ? 1 : -1;

        for (int i = 0; i < 3; i++) {
            const Emulator::Gpu::Position &a = v[i];
            const Emulator::Gpu::Position &b = v[(i + 1) % 3];

            const int e = sign * orient2d(a, b, x, y);

            if (e < 0) {
                return false;
            }

            if (e == 0 && !isTopLeft(a, b)) {
                return false;
            }
        }

        return true;
    }

    static int clipLeft(const Emulator::Gpu &gpu) { return std::clamp<int>(gpu.drawingAreaLeft, 0, WIDTH - 1); }

    static int clipTop(const Emulator::Gpu &gpu) { return std::clamp<int>(gpu.drawingAreaTop, 0, HEIGHT - 1); }

    static int clipRight(const Emulator::Gpu &gpu) {
        if (gpu.drawingAreaRight == 0 && gpu.drawingAreaBottom == 0) {
            return WIDTH - 1;
        }

        return std::clamp<int>(gpu.drawingAreaRight, 0, WIDTH - 1);
    }

    static int clipBottom(const Emulator::Gpu &gpu) {
        if (gpu.drawingAreaRight == 0 && gpu.drawingAreaBottom == 0) {
            return HEIGHT - 1;
        }

        return std::clamp<int>(gpu.drawingAreaBottom, 0, HEIGHT - 1);
    }

    static int applyTextureWindow(int v, uint8_t mask, uint8_t offset) {
        const int m = int(mask) * 8;
        const int o = int(offset & mask) * 8;
        return (v & ~m) | o;
    }

    static uint16_t sampleTexture(Emulator::Gpu &gpu, const Emulator::Gpu::UV &uv,
                                  const Emulator::Gpu::Attributes attributes) {
        int u = int(uv.u);
        int v = int(uv.v);

        u = applyTextureWindow(u, gpu.textureWindowXMask, gpu.textureWindowXOffset);
        v = applyTextureWindow(v, gpu.textureWindowYMask, gpu.textureWindowYOffset);

        const uint32_t dataX = static_cast<uint32_t>(uv.dataX);
        const uint32_t dataY = static_cast<uint32_t>(uv.dataY);

        const uint32_t clutX = dataX >> 16;
        const uint32_t pageX = dataX & 0xFFFF;
        const uint32_t clutY = dataY >> 16;
        const uint32_t pageY = dataY & 0xFFFF;

        switch (attributes.textureDepth()) {
            case 0:
                return gpu.vram->getPixel4(uint32_t(u), uint32_t(v), clutX, clutY, pageX, pageY);
            case 1:
                return gpu.vram->getPixel8(uint32_t(u), uint32_t(v), clutX, clutY, pageX, pageY);
            default:
                return gpu.vram->getPixel16(uint32_t(u), uint32_t(v), pageX, pageY);
        }
    }

    static Emulator::Gpu::UV interpolateUV(const Emulator::Gpu::Position p[3], const Emulator::Gpu::UV uv[3], int x,
                                           int y) {
        const double area = double(area2d(p[0], p[1], p[2]));

        const double w0 = double(area2d({float(x), float(y)}, p[1], p[2])) / area;
        const double w1 = double(area2d(p[0], {float(x), float(y)}, p[2])) / area;
        const double w2 = double(area2d(p[0], p[1], {float(x), float(y)})) / area;

        return {float(uv[0].u * w0 + uv[1].u * w1 + uv[2].u * w2), float(uv[0].v * w0 + uv[1].v * w1 + uv[2].v * w2),
                uv[0].dataX, uv[0].dataY};
    }

    static void drawDirectTriangle(Emulator::Gpu &gpu, const Emulator::Gpu::Position v[3],
                                   const Emulator::Gpu::Color colors[3], const Emulator::Gpu::UV uvs[3],
                                   const Emulator::Gpu::Attributes attributes) {
        const int area = area2d(v[0], v[1], v[2]);

        if (area == 0) {
            return;
        }

        int minX = int(std::min({v[0].x, v[1].x, v[2].x}));
        int maxX = int(std::max({v[0].x, v[1].x, v[2].x}));
        int minY = int(std::min({v[0].y, v[1].y, v[2].y}));
        int maxY = int(std::max({v[0].y, v[1].y, v[2].y}));

        minX = std::max(minX, clipLeft(gpu));
        maxX = std::min(maxX, clipRight(gpu));
        minY = std::max(minY, clipTop(gpu));
        maxY = std::min(maxY, clipBottom(gpu));

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                if (!insideTriangle(v, x, y)) {
                    continue;
                }

                uint16_t c;

                if (attributes.useTextures()) {
                    Emulator::Gpu::UV uv = interpolateUV(v, uvs, x, y);
                    c                    = sampleTexture(gpu, uv, attributes);

                    if ((c & 0x7FFF) == 0) {
                        continue;
                    }
                } else {
                    c = static_cast<uint16_t>(colors[0].toU32());
                }

                gpu.vram->writePixel(uint32_t(x), uint32_t(y), c);
            }
        }
    }

    static void drawDirectLine(Emulator::Gpu &gpu, const Emulator::Gpu::Position p[2],
                               const Emulator::Gpu::Color color) {
        int       x0 = int(p[0].x);
        int       y0 = int(p[0].y);
        const int x1 = int(p[1].x);
        const int y1 = int(p[1].y);

        const int cLeft   = clipLeft(gpu);
        const int cTop    = clipTop(gpu);
        const int cRight  = clipRight(gpu);
        const int cBottom = clipBottom(gpu);

        const uint16_t c = static_cast<uint16_t>(color.toU32());

        const int dx = std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;

        int err = dx + dy;

        for (;;) {
            if (x0 >= cLeft && x0 <= cRight && y0 >= cTop && y0 <= cBottom) {
                gpu.vram->writePixel(uint32_t(x0), uint32_t(y0), c);
            }

            if (x0 == x1 && y0 == y1) {
                break;
            }

            const int e2 = err * 2;

            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }

            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}*/

Emulator::Renderer::Renderer(Emulator::Gpu &gpu) : gpu(gpu), _rasterizer(gpu) {
    glfwSetErrorCallback(
            [](int code, const char *desc) { std::cerr << "GLFW error " << code << ": " << desc << '\n'; });

#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif

    if (!glfwInit()) {
        const char *desc = nullptr;
        int         code = glfwGetError(&desc);

        std::cerr << "GLFW could not initialize: " << code << ": " << (desc ? desc : "no description") << '\n';

        return;
    }

    // Set all the required options for GLFW
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    glfwWindowHint(GLFW_SAMPLES, 0);

    // glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Necessary on macOS
    // glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    // glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

    int           monitorCount;
    GLFWmonitor **monitors = glfwGetMonitors(&monitorCount);
    printf("monitorCount: %d\n", monitorCount);

    auto monitor = monitors[monitorCount - 1];
    // const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    int mx, my;
    glfwGetMonitorPos(monitor, &mx, &my);

    window = glfwCreateWindow(WIDTH, HEIGHT, "PSX", nullptr, nullptr);
    glfwSetWindowPos(window, mx + 100, my + 100);

    if (window == nullptr) {
        glfwTerminate();

        return;
    }

    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;

    // Init glew
    GLenum err = glewInit();

    if (err != GLEW_OK) {
        std::cerr << "GLEW error " << err << ": " << glewGetErrorString(err) << '\n';
        glfwTerminate();

        return;
    }

    glDisable(GL_MULTISAMPLE);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_POLYGON_SMOOTH);
    glDisable(GL_DITHER);

    // Load and bind shaders
    {
        std::string vertexSource   = getShaderSource("../../Shaders/vertex.glsl");
        std::string fragmentSource = getShaderSource("../../Shaders/fragment.glsl");

        vertexShader   = compileShader(vertexSource.c_str(), GL_VERTEX_SHADER);
        fragmentShader = compileShader(fragmentSource.c_str(), GL_FRAGMENT_SHADER);

        program = linkProgram(vertexShader, fragmentShader);
        glUseProgram(program);

        // Generate buffers & arrays
        glGenVertexArrays(1, &VAO);
        glBindVertexArray(VAO);

        // Position buffer
        positions.create();

        GLuint index = getProgramAttrib(program, "vertexPosition");

        // Enable the attrib
        glEnableVertexAttribArray(index);

        // Link the buffer and the given index.
        glVertexAttribPointer(index, 2, GL_FLOAT, GL_FALSE, sizeof(Gpu::Position), nullptr);

        // Color buffer
        colors.create();

        index = getProgramAttrib(program, "vertexColor");

        // Enable the attrib
        glEnableVertexAttribArray(index);

        // Link the buffer and the given index.
        glVertexAttribIPointer(index, 3, GL_UNSIGNED_BYTE, 0, nullptr);

        // UV Buffer
        uvs.create();

        index = getProgramAttrib(program, "texCoords");

        // Enable the attributes in the shader
        glEnableVertexAttribArray(index);

        // Link the buffer and the given index.
        glVertexAttribPointer(index, 4, GL_FLOAT, GL_FALSE, sizeof(Gpu::UV), nullptr);

        // Attributes buffer
        attributes.create();

        index = getProgramAttrib(program, "attributes");

        // Enable the attributes in the shader
        glEnableVertexAttribArray(index);

        glVertexAttribIPointer(index, 1, GL_INT, 0, nullptr);

        // Uniforms
        offsetUni = glGetUniformLocation(program, "offset");
        setDrawingOffset(0, 0);

        drawingMinUni = glGetUniformLocation(program, "drawingAreaMin");
        glUniform2i(drawingMinUni, 0, 0);

        drawingMaxUni = glGetUniformLocation(program, "drawingAreaMax");
        glUniform2i(drawingMaxUni, 1023, 511);

        textureWindowUni = glGetUniformLocation(program, "textureWindow");
        setTextureWindow(0, 0, 0, 0);

        semiTransparencyModeUni = glGetUniformLocation(program, "semiTransparencyMode");
        setSemiTransparencyMode(0);

        ditheringUni = glGetUniformLocation(program, "dithering");
        glUniform1i(ditheringUni, 0);

        maskBitUni = glGetUniformLocation(program, "setMaskBit");
        glUniform1i(maskBitUni, 0);

        pixelCenterModeUni = glGetUniformLocation(program, "pixelCenterMode");
        glUniform1i(pixelCenterModeUni, 0);
    }

    glEnable(GL_BLEND);
    // glDisable(GL_BLEND);

    for (int i = 0; i < 2; i++)
        sceneFBO[i] = createFrameBuffer(WIDTH, HEIGHT, sceneTex[i]);

    // After sceneFBO creation
    for (int i = 0; i < 2; i++) {
        bloomFBO[i] = createFrameBuffer(WIDTH, HEIGHT, bloomTexture[i]);

        /*glBindTexture(GL_TEXTURE_2D, bloomTexture[i]);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);*/
    }

    {
        // Compile vertex shader first as,
        // it's used with other shaders
        // std::string postVertexSource   = preprocessShader(readFile("Shaders/bloomVertex.glsl"), "Shader");
        /*std::string postVertexSource   = getShaderSource("../Shaders/bloomVertex.glsl");
        postProcessVertexShader = compileShader(postVertexSource.c_str(), GL_VERTEX_SHADER);

        //std::string thresholdSource = preprocessShader(readFile("Shaders/bloom_threshold.frag"), "Shaders");
        std::string thresholdSource = getShaderSource("../Shaders/bloom_threshold.frag");
        //std::string blurSource = preprocessShader(readFile("Shaders/bloom_blur.frag"), "Shaders");
        std::string blurSource = getShaderSource("../Shaders/bloom_blur.frag");

        bloomThresholdProgram = compileShader(thresholdSource.c_str(), GL_FRAGMENT_SHADER);
        blurProgram = compileShader(blurSource.c_str(), GL_FRAGMENT_SHADER);

        bloomThresholdProgram = linkProgram(postProcessVertexShader, bloomThresholdProgram);
        blurProgram = linkProgram(postProcessVertexShader, blurProgram);

        //std::string postFragmentSource = preprocessShader(readFile("Shaders/bloomFragment.glsl"), "Shader");
        std::string postFragmentSource = getShaderSource("../Shaders/bloomFragment.glsl");

        postProcessFragmentShader = compileShader(postFragmentSource.c_str(), GL_FRAGMENT_SHADER);
        postProcessProgram = linkProgram(postProcessVertexShader, postProcessFragmentShader);*/

        const char *v = R"(
            #version 330 core

            layout(location = 0) in vec2 aPos;
            layout(location = 1) in vec2 aUV;

            out vec2 vUV;

            uniform vec2 uUvMin;
            uniform vec2 uUvMax;

            void main() {
                vUV = mix(uUvMin, uUvMax, aUV);
                gl_Position = vec4(aPos, 0.0, 1.0);
            })";

        const char *f = R"(
            #version 330 core

            in vec2 vUV;
            out vec4 fragColor;

            uniform sampler2D screenTexture;

            void main() {
                fragColor = texture(screenTexture, vUV);
            }
            )";

        postProcessVertexShader   = compileShader(v, GL_VERTEX_SHADER);
        postProcessFragmentShader = compileShader(f, GL_FRAGMENT_SHADER);

        postProcessProgram = linkProgram(postProcessVertexShader, postProcessFragmentShader);

        setupScreenQuad();

#ifdef GL_DEBUG_ENABLED
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(
                [](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message,
                   const void *userParam) {
                    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION) {
                        std::cerr << "GL DEBUG [" << id << "] Severity: " << severity << " Type: " << type << "\n"
                                  << message << '\n';
                    }
                },
                nullptr);

        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
#endif

        GLenum constructorError = glGetError();
        if (constructorError != GL_NO_ERROR) {
            std::cerr << "OpenGL Error during Renderer constructor: " << constructorError << '\n';
        }
    }

    nVertices = 0;
}

static bool isRendering = false;
static bool useShaders  = false;
void        Emulator::Renderer::display(const bool displayEntireScreen) {
    if (isRendering)
        return;

    isRendering = true;

    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO[curTex]);
    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //int winWidth = WIDTH, winHeight = HEIGHT;
    //glfwGetFramebufferSize(window, &winWidth, &winHeight);

    glViewport(0, 0, WIDTH, HEIGHT);
    // glViewport(0, 0, winWidth, winHeight);

    // Render scene
    // glEnable(GL_BLEND);
    // glDisable(GL_BLEND);
    draw();
    // glDisable(GL_BLEND);

    if (!useShaders) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        //int winWidth, winHeight;
        //glfwGetFramebufferSize(window, &winWidth, &winHeight);
        //glViewport(0, 0, winWidth, winHeight);
        glViewport(0, 0, WIDTH, HEIGHT);

        glUseProgram(postProcessProgram);

        glBindVertexArray(quadVAO);

        glActiveTexture(GL_TEXTURE0);

        if (!renderVRAM)
            glBindTexture(GL_TEXTURE_2D, sceneTex[curTex]);
        else
            glBindTexture(GL_TEXTURE_2D, gpu.vram->getCurrentTexture());

        glUniform1i(glGetUniformLocation(postProcessProgram, "screenTexture"), 0);

        const bool     cropOutput  = !displayEntireScreen && !renderVRAM;
        const uint32_t displayX    = std::clamp<uint32_t>(gpu.displayVramXStart, 0, WIDTH - 1);
        const uint32_t displayY    = std::clamp<uint32_t>(gpu.displayVramYStart, 0, HEIGHT - 1);
        const uint32_t displayW    = std::clamp<uint32_t>(gpu.hres.getResolution(), 1, WIDTH - displayX);
        const uint32_t rawDisplayH = (gpu.vres == VerticalRes::Y480Lines)
                                                    ? 480
                                                    : std::max<uint16_t>(1, gpu.displayLineEnd - gpu.displayLineStart);
        const uint32_t displayH    = std::clamp<uint32_t>(rawDisplayH, 1, HEIGHT - displayY);

        const float uvMinX = cropOutput ? static_cast<float>(displayX) / static_cast<float>(WIDTH) : 0.0f;
        const float uvMaxX = cropOutput ? static_cast<float>(displayX + displayW) / static_cast<float>(WIDTH) : 1.0f;
        const float uvMinY =
                cropOutput ? 1.0f - (static_cast<float>(displayY + displayH) / static_cast<float>(HEIGHT)) : 0.0f;
        const float uvMaxY = cropOutput ? 1.0f - (static_cast<float>(displayY) / static_cast<float>(HEIGHT)) : 1.0f;

        glUniform2f(glGetUniformLocation(postProcessProgram, "uUvMin"), uvMinX, uvMinY);
        glUniform2f(glGetUniformLocation(postProcessProgram, "uUvMax"), uvMaxX, uvMaxY);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindVertexArray(0);

        isRendering = false;

        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Bloom threshold
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[0]);
    glViewport(0, 0, WIDTH, HEIGHT);

    glEnable(GL_BLEND);

    glUseProgram(bloomThresholdProgram);
    glBindVertexArray(quadVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex[curTex]);

    glUniform1i(glGetUniformLocation(bloomThresholdProgram, "thresholdImg"), 0);
    glUniform1f(glGetUniformLocation(bloomThresholdProgram, "threshold"), threshold);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[1]);
    glViewport(0, 0, WIDTH, HEIGHT);

    glEnable(GL_BLEND);

    glUseProgram(blurProgram);

    for (int i = 0; i < bloomPasses; i++) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloomTexture[horizontal]);
        // glGenerateMipmap(GL_TEXTURE_2D);
        glUniform1i(glGetUniformLocation(blurProgram, "image"), 0);
        glUniform1i(glGetUniformLocation(blurProgram, "horizontal"), horizontal ? 1 : 0);
        glUniform1f(glGetUniformLocation(blurProgram, "blurRadius"), blurRadius);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        horizontal = !horizontal;
    }

    // If the number of passes were uneven,
    // it'd cause the screen to flicker
    horizontal = true;

    glDisable(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // int winWidth = WIDTH, winHeight = HEIGHT;
    // glfwGetFramebufferSize(window, &winWidth, &winHeight);

    // Render scene
    //glViewport(0, 0, winWidth, winHeight);
    glViewport(0, 0, WIDTH, HEIGHT);
    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(postProcessProgram);
    glBindVertexArray(quadVAO);

    // Bind scene texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex[curTex]);
    // glGenerateMipmap(GL_TEXTURE_2D);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Bind bloom texture
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTexture[1]);
    // glGenerateMipmap(GL_TEXTURE_2D);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    {
        // Set uniforms
        glUniform1i(glGetUniformLocation(postProcessProgram, "screenTexture"), 0);
        glUniform1i(glGetUniformLocation(postProcessProgram, "bloomTexture"), 1);
        glUniform2f(glGetUniformLocation(postProcessProgram, "uTextureSize"), WIDTH, HEIGHT);
        glUniform2f(glGetUniformLocation(postProcessProgram, "uOutputSize"), 3840.0f, 1920.0f);

        // Sensible defaults for PS1-style rendering
        glUniform1f(glGetUniformLocation(postProcessProgram, "uKernelB"), kernelB);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uKernelC"), kernelC);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uSharpness"), sharpness);
        glUniform1i(glGetUniformLocation(postProcessProgram, "uSampleRadius"), sampleRadius);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uLODBias"), lodBias);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uEdgeThreshold"), edgeThreshold);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uDitherStrength"), ditherStrength);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uNoiseStrength"), noiseStrength);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uContrast"), contrast);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uSaturation"), saturation);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uHalation"), halation);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uTime"), glfwGetTime());
        glUniform1f(glGetUniformLocation(postProcessProgram, "uEnableAdaptiveSharpening"), enableAdaptiveSharpening);

        glUniform1f(glGetUniformLocation(postProcessProgram, "uGamma"), gamma);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uScanline"), scanline);
        glUniform1f(glGetUniformLocation(postProcessProgram, "uBloomIntensity"), bloomIntensity);

        glUniform1i(glGetUniformLocation(postProcessProgram, "uEnableBloom"), enableBloom ? 1 : 0);
        glUniform1i(glGetUniformLocation(postProcessProgram, "uEnableUpscaling"), enableUpscaling ? 1 : 0);
    }

    // Draw final quad
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);

    // glfwSwapBuffers(window);

    isRendering = false;

    /*positions.endFrame();
    colors.endFrame();
    uvs.endFrame();
    attributes.endFrame();*/
}

void Emulator::Renderer::renderFrame() {
    /*uint32_t fbX = gpu.displayVramXStart;
    uint32_t fbY = gpu.displayVramYStart;
    uint32_t fbW = gpu.hres.getResolution();                  // 256/320/368/512/640
    uint32_t fbH = (gpu.vres == VerticalRes::Y480Lines)
             ? 480
             : (gpu.display LineEnd - gpu.displayLineStart);*/

    // gpu.vram->copyToTexture(0, 0, 1024, 512, sceneTex[curTex]);

    display(!cropToDisplayArea);
    //printf("draws=%u\n", drawCalls);

    drawCalls = 0;
    curTex    = 0;
    nVertices = 0;
}

void Emulator::Renderer::flushDrawCommands() {
    if (nVertices == 0) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO[curTex]);
    glViewport(0, 0, WIDTH, HEIGHT);

    draw();

    nVertices = 0;
}

void Emulator::Renderer::readbackToVram(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }

    flushDrawCommands();

    x %= WIDTH;
    y %= HEIGHT;

    auto* const vram = gpu.vram->gpu15;

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);

    const auto readRegion = [&](uint32_t regionX, uint32_t regionY, uint32_t regionWidth, uint32_t regionHeight) {
        if (regionWidth == 0 || regionHeight == 0) {
            return;
        }

        std::vector<uint8_t> pixels(regionWidth * regionHeight * 4);

        glGetTextureSubImage(
            sceneTex[curTex],
            0,
            regionX,
            HEIGHT - regionY - regionHeight,
            0,
            regionWidth,
            regionHeight,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            static_cast<GLsizei>(pixels.size()),
            pixels.data()
        );

        for (uint32_t row = 0; row < regionHeight; row++) {
            const uint32_t psxY = regionY + row;
            const uint32_t dstY = HEIGHT - psxY - 1;
            const uint32_t srcRow = regionHeight - row - 1;

            uint16_t* dst = vram + dstY * WIDTH + regionX;

            for (uint32_t column = 0; column < regionWidth; column++) {
                const uint8_t* src = pixels.data() + ((srcRow * regionWidth + column) * 4);

                const uint16_t r = uint16_t(src[0] >> 3);
                const uint16_t g = uint16_t(src[1] >> 3);
                const uint16_t b = uint16_t(src[2] >> 3);
                const uint16_t m = src[3] >= 128 ? 0x8000 : 0;

                dst[column] = uint16_t(r | (g << 5) | (b << 10) | m);
            }
        }
    };

    uint32_t remainingHeight = height;
    uint32_t currentY = y;

    while (remainingHeight > 0) {
        const uint32_t chunkHeight = std::min<uint32_t>(remainingHeight, HEIGHT - currentY);

        uint32_t remainingWidth = width;
        uint32_t currentX = x;

        while (remainingWidth > 0) {
            const uint32_t chunkWidth = std::min<uint32_t>(remainingWidth, WIDTH - currentX);

            readRegion(currentX, currentY, chunkWidth, chunkHeight);

            remainingWidth -= chunkWidth;
            currentX = 0;
        }

        remainingHeight -= chunkHeight;
        currentY = 0;
    }
}

void Emulator::Renderer::draw() {
    if (nVertices == 0)
        return;

    if (!positions.map || !colors.map || !uvs.map || !attributes.map) {
        std::cerr << "Persistent buffer unmapped or invalid!" << std::endl;
        nVertices = 0;
        return;
    }

    if (drawFence) {
        glClientWaitSync(drawFence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
        glDeleteSync(drawFence);
        drawFence = nullptr;
    }

    drawCalls++;

    positions.flush();
    colors.flush();
    uvs.flush();
    attributes.flush();

    glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO[curTex]);
    glViewport(0, 0, WIDTH, HEIGHT);

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_MULTISAMPLE);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_POLYGON_SMOOTH);
    glDisable(GL_DITHER);

    glLineWidth(1.0f);
    glPointSize(1.0f);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glUseProgram(program);

    glUniform1i(ditheringUni, gpu.dithering ? 1 : 0);
    glUniform1i(maskBitUni, gpu.forceSetMaskBit ? 1 : 0);
    glUniform1i(pixelCenterModeUni, primitiveMode == GL_TRIANGLES ? 0 : 1);

    glBindVertexArray(VAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gpu.vram->getCurrentTexture());

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sceneTex[curTex]);

    glDrawArrays(primitiveMode, 0, static_cast<GLsizei>(nVertices));
    /*if (primitiveMode == GL_TRIANGLES && gpu.semiTransparencyEnabled) {
        for (uint32_t i = 0; i + 2 < nVertices; i += 3) {
            glTextureBarrier();
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(i), 3);
        }
    } else {
        glDrawArrays(primitiveMode, 0, static_cast<GLsizei>(nVertices));
    }*/

    glBindVertexArray(0);
    glUseProgram(0);

    drawFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    /*glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO[curTex]);
    glViewport(0, 0, WIDTH, HEIGHT);

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_MULTISAMPLE);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_POLYGON_SMOOTH);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glUseProgram(program);
    glUniform1i(ditheringUni, gpu.dithering ? 1 : 0);
    glUniform1i(maskBitUni, gpu.forceSetMaskBit ? 1 : 0);
    glBindVertexArray(VAO);

    positions.flush();
    colors.flush();
    uvs.flush();
    attributes.flush();

    glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gpu.vram->getCurrentTexture());

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sceneTex[curTex]);

    for (uint32_t i = 0; i + 2 < nVertices; i += 3) {
        glTextureBarrier();
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(i), 3);
    }

    glBindVertexArray(0);
    glUseProgram(0);*/

    /*glDisable(GL_BLEND);

    glUseProgram(program);
    glUniform1i(ditheringUni, gpu.dithering ? 1 : 0);
    glBindVertexArray(VAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gpu.vram->getCurrentTexture());

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sceneTex[curTex]);

    for (uint32_t i = 0; i + 2 < nVertices; i += 3) {
        glTextureBarrier();
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(i), 3);
    }

    glBindVertexArray(0);
    glUseProgram(0);*/
}

void Emulator::Renderer::clear() {
    /*glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);*/
}

static Emulator::Gpu::Position biasedVertex(const Emulator::Gpu::Position& p, float cx, float cy) {
    constexpr float EPS = 1.0f / 256.0f;

    Emulator::Gpu::Position out = p;

    if (out.x > cx) {
        out.x += EPS;
    } else if (out.x < cx) {
        out.x -= EPS;
    }

    if (out.y > cy) {
        out.y += EPS;
    } else if (out.y < cy) {
        out.y -= EPS;
    }

    return out;
}

static void biasTriangleForGL(const Emulator::Gpu::Position in[3], Emulator::Gpu::Position out[3]) {
    const float cx = (in[0].x + in[1].x + in[2].x) / 3.0f;
    const float cy = (in[0].y + in[1].y + in[2].y) / 3.0f;

    out[0] = biasedVertex(in[0], cx, cy);
    out[1] = biasedVertex(in[1], cx, cy);
    out[2] = biasedVertex(in[2], cx, cy);
}

void Emulator::Renderer::pushLine(Emulator::Gpu::Position positions[], Emulator::Gpu::Color colors[],
                                  Emulator::Gpu::UV uvs[], Gpu::Attributes attributes) {
    if (nVertices + 2 > VERTEX_BUFFER_LEN) {
        nVertices = 0;
    }

    if (positions[0].x == positions[1].x && positions[0].y == positions[1].y) {
        setPrimitiveMode(GL_POINTS);

        if (nVertices + 1 > VERTEX_BUFFER_LEN) {
            flushDrawCommands();
        }

        this->positions.set(nVertices, positions[0]);

        if (attributes.usesColor()) {
            this->colors.set(nVertices, colors[0]);
        }

        if (attributes.useTextures()) {
            this->uvs.set(nVertices, uvs[0]);
        }

        this->attributes.set(nVertices, attributes);
        nVertices++;

        return;
    }

    setPrimitiveMode(GL_LINES);

    if (nVertices + 2 > VERTEX_BUFFER_LEN) {
        flushDrawCommands();
    }

    for (int i = 0; i < 2; i++) {
        this->positions.set(nVertices, positions[i]);

        if (attributes.usesColor()) {
            this->colors.set(nVertices, colors[i]);
        }

        if (attributes.useTextures()) {
            this->uvs.set(nVertices, uvs[i]);
        }

        this->attributes.set(nVertices, attributes);
        nVertices++;
    }

    flushDrawCommands();

    setPrimitiveMode(GL_POINTS);

    if (nVertices + 1 > VERTEX_BUFFER_LEN) {
        flushDrawCommands();
    }

    this->positions.set(nVertices, positions[1]);

    if (attributes.usesColor()) {
        this->colors.set(nVertices, colors[1]);
    }

    if (attributes.useTextures()) {
        this->uvs.set(nVertices, uvs[1]);
    }

    this->attributes.set(nVertices, attributes);
    nVertices++;

    return;

    /*drawDirectLine(gpu, positions, colors[0]);

    return;*/

    for (int i = 0; i < 2; i++) {
        this->positions.set(nVertices, positions[i]);

        if (attributes.usesColor()) {
            this->colors.set(nVertices, colors[i]);
        }

        if (attributes.useTextures()) {
            this->uvs.set(nVertices, uvs[i]);
        }

        this->attributes.set(nVertices, attributes);
        nVertices++;
    }

    return;

    //_rasterizer.drawLine(positions, colors, uvs, attributes);

    float dx = positions[1].x - positions[0].x;
    float dy = positions[1].y - positions[0].y;

    float len = sqrtf(dx * dx + dy * dy);
    if (len == 0) {
        // assert(false && "Uhhh len is 0?? (pushLine)");
        // return;
    }

    dx /= len;
    dy /= len;

    float px = -dy;
    float py = dx;

    float thickness = 1;
    float ox        = px * (thickness * 0.5f);
    float oy        = py * (thickness * 0.5f);

    Emulator::Gpu::Position v0{positions[0].x + ox, positions[0].y + oy};
    Emulator::Gpu::Position v1{positions[1].x + ox, positions[1].y + oy};
    Emulator::Gpu::Position v2{positions[1].x - ox, positions[1].y - oy};
    Emulator::Gpu::Position v3{positions[0].x - ox, positions[0].y - oy};

    Emulator::Gpu::Color c0 = colors[0];
    Emulator::Gpu::Color c1 = colors[0];
    Emulator::Gpu::Color c2 = colors[1];
    Emulator::Gpu::Color c3 = colors[1];

    {
        Gpu::Position pos[3] = {v0, v1, v2};
        Gpu::Color    col[3] = {c0, c1, c2};
        pushTriangle(pos, col, {}, attributes);
    }

    {
        Gpu::Position pos[3] = {v2, v3, v0};
        Gpu::Color    col[3] = {c2, c3, c0};
        pushTriangle(pos, col, {}, attributes);
    }
}

void Emulator::Renderer::pushTriangle(Emulator::Gpu::Position positions[], Emulator::Gpu::Color colors[],
                                      Emulator::Gpu::UV uvs[], Gpu::Attributes attributes) {
    if (nVertices + 3 > VERTEX_BUFFER_LEN) {
        // Reset the buffer size
        nVertices = 0;

        // display();
    }
    setPrimitiveMode(GL_TRIANGLES);

    Emulator::Gpu::Position biased[3];
    biasTriangleForGL(positions, biased);

    for (int i = 0; i < 3; i++) {
        this->positions.set(nVertices, biased[i]);

        if (attributes.usesColor()) {
            this->colors.set(nVertices, colors[i]);
        }

        if (attributes.useTextures()) {
            this->uvs.set(nVertices, uvs[i]);
        }

        this->attributes.set(nVertices, attributes);
        nVertices++;
    }

    return;

    /*drawDirectTriangle(gpu, positions, colors, uvs, attributes);

    return;*/

    for (int i = 0; i < 3; i++) {
        this->positions.set(nVertices, positions[i]);

        if (attributes.usesColor())
            this->colors.set(nVertices, colors[i]);

        if (attributes.useTextures())
            this->uvs.set(nVertices, uvs[i]);

        this->attributes.set(nVertices, attributes);

        nVertices++;
    }
}

void Emulator::Renderer::pushQuad(Emulator::Gpu::Position positions[], Emulator::Gpu::Color colors[],
                                  Emulator::Gpu::UV uvs[], Gpu::Attributes attributes) {
    if (nVertices + 6 > VERTEX_BUFFER_LEN) {
        // Reset the buffer size
        nVertices = 0;

        // display();
    }
    setPrimitiveMode(GL_TRIANGLES);

    /*Emulator::Gpu::Position p0[3] = {positions[1], positions[3], positions[2]};
    Emulator::Gpu::Color c0[3] = {colors[1], colors[3], colors[2]};
    Emulator::Gpu::UV u0[3] = {uvs[1], uvs[3], uvs[2]};

    Emulator::Gpu::Position p1[3] = {positions[0], positions[1], positions[2]};
    Emulator::Gpu::Color c1[3] = {colors[0], colors[1], colors[2]};
    Emulator::Gpu::UV u1[3] = {uvs[0], uvs[1], uvs[2]};

    drawDirectTriangle(gpu, p0, c0, u0, attributes);
    drawDirectTriangle(gpu, p1, c1, u1, attributes);

    return;*/

    constexpr int order[6] = {1, 3, 2, 0, 1, 2};

    for (int i: order) {
        this->positions.set(nVertices, positions[i]);

        if (attributes.usesColor()) {
            this->colors.set(nVertices, colors[i]);
        }

        if (attributes.useTextures()) {
            this->uvs.set(nVertices, uvs[i]);
        }

        this->attributes.set(nVertices, attributes);
        nVertices++;
    }

    return;

    _rasterizer.drawQuad(positions, colors, uvs, attributes);
    // displayVRam();

    // First triangle
    // [2, 3, 0]

    this->positions.set(nVertices, positions[2]);
    if (attributes.usesColor())
        this->colors.set(nVertices, colors[2]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[2]);
    this->attributes.set(nVertices, attributes);
    nVertices++;

    this->positions.set(nVertices, positions[3]);
    if (attributes.usesColor())
        this->colors.set(nVertices, colors[3]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[3]);
    this->attributes.set(nVertices, attributes);
    nVertices++;

    this->positions.set(nVertices, positions[0]);
    if (attributes.usesColor())
        this->colors.set(nVertices, colors[0]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[0]);
    this->attributes.set(nVertices, attributes);
    nVertices++;

    this->positions.set(nVertices, positions[3]);
    if (attributes.usesColor())
        this->colors.set(nVertices, colors[3]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[3]);
    this->attributes.set(nVertices, attributes);
    nVertices++;

    this->positions.set(nVertices, positions[0]);
    if (attributes.usesColor())
        this->colors.set(nVertices, colors[0]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[0]);
    this->attributes.set(nVertices, attributes);
    nVertices++;

    this->positions.set(nVertices, positions[1]);
    if (attributes.usesColor())
        this->colors.set(nVertices, colors[1]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[1]);
    this->attributes.set(nVertices, attributes);
    nVertices++;
}

void Emulator::Renderer::pushRectangle(Emulator::Gpu::Position positions[], Emulator::Gpu::Color colors[],
                                       Emulator::Gpu::UV uvs[], Gpu::Attributes attributes) {
    /*
     * From my knowledgeable, PS1 doesn't split,
     * rectangles into 2 triangles, however,
     * I don't understand SDL enough..
     * So, my goal in the future is to use,
     * a different library because I hate SDL.
     *
     * Hence, why this is a different function,
     * from pushQuad, even though my goal isn't,
     * to make this emulator anywhere near accurate,
     * but it's fun to learn new stuff
     */

    if (nVertices + 6 > VERTEX_BUFFER_LEN) {
        // Reset the buffer size
        nVertices = 0;

        // display();
    }

    setPrimitiveMode(GL_TRIANGLES);

    /*int left = int(positions[0].x);
    int top = int(positions[0].y);
    int right = int(positions[3].x);
    int bottom = int(positions[3].y);

    if (right < left) {
        std::swap(left, right);
    }

    if (bottom < top) {
        std::swap(top, bottom);
    }

    left = std::max(left, clipLeft(gpu));
    right = std::min(right, clipRight(gpu) + 1);
    top = std::max(top, clipTop(gpu));
    bottom = std::min(bottom, clipBottom(gpu) + 1);

    const int baseX = int(positions[0].x);
    const int baseY = int(positions[0].y);

    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            uint16_t c;

            if (attributes.useTextures()) {
                Emulator::Gpu::UV uv = uvs[0];
                uv.u += float(x - baseX);
                uv.v += float(y - baseY);

                c = sampleTexture(gpu, uv, attributes);

                if ((c & 0x7FFF) == 0) {
                    continue;
                }
            } else {
                c = static_cast<uint16_t>(colors[0].toU32());
            }

            gpu.vram->writePixel(uint32_t(x), uint32_t(y), c);
        }
    }

    return;*/

    _rasterizer.drawRectangle(positions, colors, uvs, attributes);

    // First triangle
    // [0, 1, 2]
    this->positions.set(nVertices, positions[0]);
    this->colors.set(nVertices, colors[0]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[0]);
    this->attributes.set(nVertices, attributes);
    nVertices++;

    this->positions.set(nVertices, positions[1]);
    this->colors.set(nVertices, colors[1]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[1]);
    this->attributes.set(nVertices, attributes);
    nVertices++;

    this->positions.set(nVertices, positions[2]);
    this->colors.set(nVertices, colors[2]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[2]);
    this->attributes.set(nVertices, attributes);
    nVertices++;

    // First triangle
    // [1, 2, 3]
    this->positions.set(nVertices, positions[1]);
    this->colors.set(nVertices, colors[1]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[1]);
    this->attributes.set(nVertices, attributes);
    nVertices++;

    this->positions.set(nVertices, positions[2]);
    this->colors.set(nVertices, colors[2]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[2]);
    this->attributes.set(nVertices, attributes);
    nVertices++;

    this->positions.set(nVertices, positions[3]);
    this->colors.set(nVertices, colors[3]);
    if (attributes.useTextures())
        this->uvs.set(nVertices, uvs[3]);
    this->attributes.set(nVertices, attributes);
    nVertices++;
}

void Emulator::Renderer::setDrawingOffset(int16_t x, int16_t y) {
    /**
     * Because my renderer is still...
     * isn't completed I need to manually do this,
     * for every game as otherwise, it causes,
     * the entire screen to constantly flicker
     */

    glUseProgram(program);

    // int16_t f = 256;
    // int16_t d = 128;

    // glUniform2f(offsetUni, x, y);
    glUniform2f(offsetUni, 0, 0);
    // lUniform2i(offsetUni, 0, 256);
    // glUniform2i(offsetUni, 0, 0); // GEX
    // glUniform2i(offsetUni, 120, 160); // Pepsi man
    // glUniform2i(offsetUni, 0, 0); // Ridge Racer
    // glUniform2f(offsetUni, f, d); // Crash Bandicoot
}

void Emulator::Renderer::setDrawingArea(const uint16_t left, const uint16_t right, const uint16_t top,
                                        const uint16_t bottom) const {
    glUseProgram(program);

    glUniform2i(drawingMinUni, left, top);
    glUniform2i(drawingMaxUni, right, bottom);
}

void Emulator::Renderer::setTextureWindow(uint8_t textureWindowXMask, uint8_t textureWindowYMask,
                                          uint8_t textureWindowXOffset, uint8_t textureWindowYOffset) {
    glUseProgram(program);

    glUniform4f(textureWindowUni, textureWindowXMask, textureWindowYMask, textureWindowXOffset, textureWindowYOffset);
}

void Emulator::Renderer::setSemiTransparencyMode(uint8_t semiTransparencyMode) const {
    glUseProgram(program);

    // printf("Got; %d\n", semiTransparencyMode);
    glUniform1i(semiTransparencyModeUni, semiTransparencyMode);
}

void Emulator::Renderer::bindFrameBuffer(GLuint buf) {
    glBindFramebuffer(GL_FRAMEBUFFER, buf);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

GLuint Emulator::Renderer::compileShader(const char *source, const GLenum shaderType) {
    const GLuint shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);

        std::cerr << "ERROR::SHADER::COMPILATION_FAILED\n" << infoLog << '\n';
    }

    return shader;
}

GLuint Emulator::Renderer::linkProgram(GLuint vertexShader, GLuint fragmentShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (success != GL_TRUE) {
        GLchar infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << '\n';
    }

    return program;
}

GLuint Emulator::Renderer::getProgramAttrib(GLuint program, const std::string &attr) {
    GLint index = glGetAttribLocation(program, attr.c_str());

    if (index < 0) {
        std::cerr << "Attribute " << attr << " was not found in the program\n";
        return -1;
    }

    return static_cast<GLuint>(index);
}

std::string Emulator::Renderer::getShaderSource(const std::string &path) {
    std::filesystem::path absPath = std::filesystem::absolute(path);

    std::ifstream file(absPath /*, std::ios::in | std::ios::binary*/);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open the file at path: " << absPath << '\n';
        return {};
    }

    std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    // Close the file
    file.close();

    // Print loaded file full path
    std::cout << "Loaded shader from: " << absPath << '\n';

    return content;
}

GLuint Emulator::Renderer::createFrameBuffer(GLsizei width, GLsizei height, GLuint &textureId) {
    GLuint framebuffer;
    glGenFramebuffers(1, &framebuffer);

    glGenTextures(1, &textureId);

    glBindTexture(GL_TEXTURE_2D, textureId);

    // glGenerateMipmap(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << '\n';

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return framebuffer;
}

void Emulator::Renderer::setupScreenQuad() {
    float fullscreenQuad[] = {// positions   // texCoords
                              -1.0f, 1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,

                              -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f};

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(fullscreenQuad), fullscreenQuad, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *) (2 * sizeof(float)));

    glBindVertexArray(0);
}

void Emulator::Renderer::setPrimitiveMode(GLenum mode) {
    if (primitiveMode == mode) {
        return;
    }

    flushDrawCommands();
    primitiveMode = mode;
}
