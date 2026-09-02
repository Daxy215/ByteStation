cmake_minimum_required(VERSION 3.10)
project(ByteStation)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(GLEW REQUIRED)

include_directories(
        ${CMAKE_SOURCE_DIR}/src
        ${OPENGL_INCLUDE_DIRS}
        ${GLEW_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b/backends
        ${CMAKE_SOURCE_DIR}/libs/glm
)

file(GLOB_RECURSE SOURCES
        ${CMAKE_SOURCE_DIR}/src/Core/*.cpp
        ${CMAKE_SOURCE_DIR}/src/CPU/*.cpp
        ${CMAKE_SOURCE_DIR}/src/CPU/COP/*.cpp
        ${CMAKE_SOURCE_DIR}/src/CPU/COP/Stolen/gte/*.cpp
        ${CMAKE_SOURCE_DIR}/src/DMA/*.cpp
        ${CMAKE_SOURCE_DIR}/src/GPU/*.cpp
        ${CMAKE_SOURCE_DIR}/src/GPU/Rendering/*.cpp
        ${CMAKE_SOURCE_DIR}/src/Memory/*.cpp
        ${CMAKE_SOURCE_DIR}/src/Memory/Bios/*.cpp
        ${CMAKE_SOURCE_DIR}/src/Memory/CDROM/*.cpp
        ${CMAKE_SOURCE_DIR}/src/Memory/IO/*.cpp
        ${CMAKE_SOURCE_DIR}/src/Memory/IO/Peripherals/*.cpp
        ${CMAKE_SOURCE_DIR}/src/Memory/IO/Memories/*.cpp
        ${CMAKE_SOURCE_DIR}/src/Memory/Timers/*.cpp
        ${CMAKE_SOURCE_DIR}/src/SPU/*.cpp
        ${CMAKE_SOURCE_DIR}/src/SPU/Stolen/*.cpp
        ${CMAKE_SOURCE_DIR}/src/SPU/Utils/*.cpp
        ${CMAKE_SOURCE_DIR}/src/SPU/Utils/FileSystem/*.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b/imgui.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b/imgui_demo.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b/imgui_draw.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b/imgui_tables.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b/imgui_widgets.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b/imstb_rectpack.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b/imstb_textedit.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b/imstb_truetype.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b/backends/imgui_impl_glfw.cpp
        ${CMAKE_SOURCE_DIR}/libs/imgui-1.91.9b/backends/imgui_impl_opengl3.cpp
)

# Its being gay
list(APPEND SOURCES
        ${CMAKE_SOURCE_DIR}/src/GPU/GPUTests.cpp
)

add_definitions(-DGLEW_STATIC)

add_executable(ByteStation ${SOURCES})

target_link_libraries(ByteStation PRIVATE
        ${OPENGL_LIBRARIES}
        glfw
        SDL2::SDL2
        GLEW::GLEW
)

target_include_directories(ByteStation PRIVATE
    ${CMAKE_SOURCE_DIR}/src/CPU/COP/Stolen/gte
)

set(LIBS_DIR "${CMAKE_SOURCE_DIR}/../libs")
set(NLOHMANN_PATH ${LIBS_DIR}/nlohmann)
add_library(nlohmann INTERFACE)
target_include_directories(nlohmann INTERFACE ${NLOHMANN_PATH})
target_link_libraries(ByteStation PRIVATE nlohmann)

#include_directories(${CMAKE_SOURCE_DIR}/src)
#include_directories(${OPENGL_INCLUDE_DIRS})
#
#set(LIBS_DIR "${CMAKE_SOURCE_DIR}/../libs")
#
## Check if it exists
##if (EXISTS "${LIBS_DIR}")
##    message(STATUS "Found libs folder at: ${LIBS_DIR}")
##else()
##    message(FATAL_ERROR "'libs' folder not found at: ${LIBS_DIR}\n"
##            "Expected structure:\n"
##            "  ${CMAKE_SOURCE_DIR}/../libs/\n"
##            "Make sure your project root contains 'libs/'.")
##endif()
#
## Files to be included
#file(GLOB_RECURSE SOURCES
#    ${CMAKE_SOURCE_DIR}/src/Core/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/CPU/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/CPU/COP/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/CPU/COP/Stolen/gte/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/DMA/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/GPU/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/GPU/Rendering/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/Memory/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/Memory/Bios/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/Memory/CDROM/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/Memory/IO/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/Memory/IO/Peripherals/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/Memory/IO/Memories/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/Memory/Timers/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/SPU/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/SPU/Stolen/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/SPU/Utils/*.cpp
#    ${CMAKE_SOURCE_DIR}/src/SPU/Utils/FileSystem/*.cpp
#
#    # ImGui sources
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/imconfig.h
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/imgui.cpp
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/imgui_demo.cpp
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/imgui_draw.cpp
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/imgui_internal.h
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/imgui_tables.cpp
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/imgui_widgets.cpp
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/imstb_rectpack.cpp
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/imstb_textedit.cpp
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/imstb_truetype.cpp
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/backends/imgui_impl_glfw.cpp
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/backends/imgui_impl_opengl3.cpp
#
#    # GLM sources
#    # ${CMAKE_SOURCE_DIR}/../libs/glm/glm/*.cpp
#
#    # Glew sources
#    # Is this even needed?
#    #${CMAKE_SOURCE_DIR}/../libs/glew-2.2.0/include/GL/glew.h
#    #${CMAKE_SOURCE_DIR}/../libs/glew-2.2.0/src/glew.c
#)
#
#set(NLOHMANN_PATH ${LIBS_DIR}/nlohmann)
#add_library(nlohmann INTERFACE)
#target_include_directories(nlohmann INTERFACE ${NLOHMANN_PATH})
#
#add_executable(ByteStation ${SOURCES}
#        # I am lazy :D
#        Memory/MDEC/MDEC.cpp
#        Memory/MDEC/MDEC.h)
#
##add_definitions(-DGLEW_STATIC)
#
## Uhh just for testing
##set(IPP_INCLUDE_DIR /opt/intel/oneapi/2025.3/include)
##set(IPP_LIB_DIR     /opt/intel/oneapi/2025.3/lib)
#
#target_link_libraries(ByteStation
#        ${OPENGL_LIBRARIES}
#        glfw
#        SDL2::SDL2
#        #GLEW
#        #X11
#        #dl
#        #pthread
#        nlohmann
#        #${IPP_LIB_DIR}/libippcc.so
#)
#
#target_include_directories(ByteStation PRIVATE
#    ${IPP_INCLUDE_DIR}
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/
#    ${CMAKE_SOURCE_DIR}/../libs/imgui-1.91.9b/backends
#    ${CMAKE_SOURCE_DIR}/../libs/glm
#    ${CMAKE_SOURCE_DIR}/../libs/glew-2.2.0/include
#    ${CMAKE_SOURCE_DIR}/src/Core/
#    ${CMAKE_SOURCE_DIR}/src/CPU
#    ${CMAKE_SOURCE_DIR}/src/CPU/COP
#    ${CMAKE_SOURCE_DIR}/src/CPU/COP/Stolen/gte
#    ${CMAKE_SOURCE_DIR}/src/DMA
#    ${CMAKE_SOURCE_DIR}/src/GPU
#    ${CMAKE_SOURCE_DIR}/src/GPU/Rendering
#    ${CMAKE_SOURCE_DIR}/src/Memory
#    ${CMAKE_SOURCE_DIR}/src/Memory/Bios
#    ${CMAKE_SOURCE_DIR}/src/Memory/CDROM
#    ${CMAKE_SOURCE_DIR}/src/Memory/IO
#    ${CMAKE_SOURCE_DIR}/src/Memory/IO/Peripherals
#    ${CMAKE_SOURCE_DIR}/src/Memory/IO/Memories
#    ${CMAKE_SOURCE_DIR}/src/Memory/Timers
#    ${CMAKE_SOURCE_DIR}/src/SPU
#    ${CMAKE_SOURCE_DIR}/src/SPU/Stolen
#    ${CMAKE_SOURCE_DIR}/src/SPU/Utils
#    ${CMAKE_SOURCE_DIR}/src/SPU/Utils/FileSystem
#)
