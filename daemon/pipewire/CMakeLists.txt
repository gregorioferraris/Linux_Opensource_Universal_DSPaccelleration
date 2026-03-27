cmake_minimum_required(VERSION 3.20)
project(DspAccelDaemon VERSION 0.2.0 LANGUAGES C)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(VULKAN   REQUIRED vulkan)

# ─── Vulkan Backend ───────────────────────────────────────────────────────────
add_library(dsp_vulkan STATIC
    ../vulkan/vulkan_compute.c
)
target_include_directories(dsp_vulkan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../vulkan
    ${VULKAN_INCLUDE_DIRS}
)
target_link_libraries(dsp_vulkan ${VULKAN_LIBRARIES})

# ─── DSP Accel Worker (Specialized Process) ──────────────────────────────────
add_executable(dsp-accel-worker
    ../worker.cpp
)
target_include_directories(dsp-accel-worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../vulkan
    ${CMAKE_CURRENT_SOURCE_DIR}/../ipc/include
    ${VULKAN_INCLUDE_DIRS}
)
target_link_libraries(dsp-accel-worker
    dsp_vulkan
    ${VULKAN_LIBRARIES}
    Threads::Threads
)

# ─── PipeWire Proxy Module ──────────────────────────────────────────────────
# Note: This module NO LONGER links dsp_vulkan directly to avoid 
# crashing the PipeWire process if Vulkan/Driver fails.
add_library(module-dsp-accel MODULE
    module-dsp-accel.cpp
)
target_include_directories(module-dsp-accel PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../clap/extension
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ipc/include
)
target_link_libraries(module-dsp-accel
    ${PIPEWIRE_LIBRARIES}
    Threads::Threads
)
set_target_properties(module-dsp-accel PROPERTIES PREFIX "")

# ─── Compile GLSL Shader to SPIR-V ────────────────────────────────────────────
find_program(GLSLC glslc)
if(GLSLC)
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/dsp_shader.spv
        COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/../vulkan/dsp_shader.comp -o ${CMAKE_BINARY_DIR}/dsp_shader.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../vulkan/dsp_shader.comp
    )
    add_custom_target(dsp_shader ALL DEPENDS ${CMAKE_BINARY_DIR}/dsp_shader.spv)
    target_compile_definitions(dsp_vulkan PRIVATE SHADER_PATH="${CMAKE_BINARY_DIR}/dsp_shader.spv")
endif()
