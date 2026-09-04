#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <GL/glew.h>

constexpr unsigned int VERTEX_BUFFER_LEN = (640 * 240) * 2;
constexpr unsigned int BUFFER_REGIONS = 32;

template<typename T>
struct Buffer {
    GLuint object = 0;
    T* map = nullptr;
    GLsizeiptr bufferSize = 0;
    uint32_t dirtyStart = std::numeric_limits<uint32_t>::max();
    uint32_t dirtyEnd = 0;

    ~Buffer() {
        if (object != 0) {
            glBindBuffer(GL_ARRAY_BUFFER, object);

            if (map) {
                glUnmapBuffer(GL_ARRAY_BUFFER);
                map = nullptr;
            }

            glDeleteBuffers(1, &object);
            object = 0;
        }
    }

    void create() {
        glGenBuffers(1, &object);
        glBindBuffer(GL_ARRAY_BUFFER, object);

        bufferSize = GLsizeiptr(sizeof(T) * VERTEX_BUFFER_LEN * BUFFER_REGIONS);

        const GLbitfield storageFlags =
            GL_MAP_WRITE_BIT |
            GL_MAP_PERSISTENT_BIT;

        glBufferStorage(GL_ARRAY_BUFFER, bufferSize, nullptr, storageFlags);

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            throw std::runtime_error("glBufferStorage failed");
        }

        const GLbitfield mapFlags =
            GL_MAP_WRITE_BIT |
            GL_MAP_PERSISTENT_BIT |
            GL_MAP_FLUSH_EXPLICIT_BIT;

        map = static_cast<T*>(
            glMapBufferRange(GL_ARRAY_BUFFER, 0, bufferSize, mapFlags)
        );

        if (!map) {
            throw std::runtime_error("glMapBufferRange failed");
        }

        std::memset(map, 0, size_t(bufferSize));

        dirtyStart = 0;
        dirtyEnd = VERTEX_BUFFER_LEN * BUFFER_REGIONS;
        flush();
    }

    void set(uint32_t index, const T& value) {
        if (index >= VERTEX_BUFFER_LEN * BUFFER_REGIONS) {
            throw std::runtime_error("buffer overflow");
        }

        if (!map) {
            throw std::runtime_error("buffer not mapped");
        }

        map[index] = value;

        dirtyStart = std::min(dirtyStart, index);
        dirtyEnd = std::max(dirtyEnd, index + 1);
    }

    void flush() {
        if (!map || dirtyStart >= dirtyEnd) {
            return;
        }

        const GLintptr offset = GLintptr(dirtyStart * sizeof(T));
        const GLsizeiptr size = GLsizeiptr((dirtyEnd - dirtyStart) * sizeof(T));

        glBindBuffer(GL_ARRAY_BUFFER, object);
        glFlushMappedBufferRange(GL_ARRAY_BUFFER, offset, size);

        dirtyStart = std::numeric_limits<uint32_t>::max();
        dirtyEnd = 0;
    }
};
