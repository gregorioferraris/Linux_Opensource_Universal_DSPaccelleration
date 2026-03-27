cmake_minimum_required(VERSION 3.20)
project(LinuxDspAcceleration VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# ─── SDK (shared between both worlds) ────────────────────────────────────────
include_directories(sdk/include sdk/platform)

# ─── World 1: CLAP Plugin Side ───────────────────────────────────────────────
# The CLAP extension header and the example plugin
add_subdirectory(clap/plugin_example)

# ─── World 2: PipeWire Daemon Side ───────────────────────────────────────────
add_subdirectory(daemon/pipewire)

# ─── Tests & Verification ───────────────────────────────────────────
add_subdirectory(tests)

# ─── Examples ──────────────────────────────────────────────────────
add_subdirectory(examples/hello_world_plugin)
