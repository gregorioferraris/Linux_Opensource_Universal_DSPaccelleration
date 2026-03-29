cmake_minimum_required(VERSION 3.20)

file(READ "VERSION" PROJECT_VERSION_RAW)
string(STRIP "${PROJECT_VERSION_RAW}" PROJECT_VERSION_RAW)

# Extract numeric part (e.g. 0.1.0) for CMake's project() version compatibility
if(PROJECT_VERSION_RAW MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)")
    set(PROJECT_VERSION_CLEAN "${CMAKE_MATCH_1}")
else()
    set(PROJECT_VERSION_CLEAN "0.1.0")
endif()

project(LinuxDSPAcceleration VERSION ${PROJECT_VERSION_CLEAN} LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# ─── Validation ──────────────────────────────────────────────────────────────
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/clap-sdk/include/clap/clap.h")
    message(FATAL_ERROR 
        "CLAP SDK not found at ${CMAKE_CURRENT_SOURCE_DIR}/clap-sdk.\n"
        "Please run: ./setup_linux_env.sh to download dependencies.")
endif()

# ─── SDK (shared between both worlds) ────────────────────────────────────────
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include 
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/platform 
    ${CMAKE_CURRENT_SOURCE_DIR}/clap-sdk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/daemon/ipc/include
)

# ─── Shader Compilation ──────────────────────────────────────────────────────
find_program(GLSLANG_VALIDATOR glslangValidator)
if(GLSLANG_VALIDATOR)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/dsp_shader.spv
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/daemon/vulkan/dsp_shader.comp -o ${CMAKE_BINARY_DIR}/dsp_shader.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/daemon/vulkan/dsp_shader.comp
        COMMENT "Compiling Vulkan Compute Shader to SPIR-V"
    )
    add_custom_target(CompiledShaders ALL DEPENDS ${CMAKE_BINARY_DIR}/dsp_shader.spv)
endif()

# ─── World 1: CLAP Plugin Side ───────────────────────────────────────────────
# The CLAP extension header and the example plugin
add_subdirectory(clap/plugin_example)

# ─── World 2: PipeWire Daemon Side ───────────────────────────────────────────
# The main daemon executable
add_executable(dsp_accel_daemon daemon/main.cpp)
target_link_libraries(dsp_accel_daemon PRIVATE Threads::Threads) # Link with pthreads for watchdog

# The worker executable (actual hardware processing)
add_executable(dsp_accel_worker daemon/worker_main.cpp daemon/vulkan/vulkan_compute.cpp)
target_link_libraries(dsp_accel_worker PRIVATE Threads::Threads)

# ─── Tests & Verification ───────────────────────────────────────────
add_subdirectory(tests)

# ─── Examples ──────────────────────────────────────────────────────
add_subdirectory(examples/hello_world_plugin)
