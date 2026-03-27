cmake_minimum_required(VERSION 3.20)
project(DspAccelPlugin VERSION 0.2.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

# CLAP extension headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../extension)

add_library(dsp_accel_plugin SHARED 
    src/plugin.cpp
)

target_include_directories(dsp_accel_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..  # project root for sdk/
)

set_target_properties(dsp_accel_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)
