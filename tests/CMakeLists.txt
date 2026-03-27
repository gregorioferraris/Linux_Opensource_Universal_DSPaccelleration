cmake_minimum_required(VERSION 3.20)
project(DspAccelTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

include_directories(../)

add_executable(test_core test_core.cpp)
add_executable(simulation_crash simulation_crash.cpp)
add_executable(test_control_simulacrum test_control_simulacrum.cpp)

# Output tests in the main build folder
set_target_properties(test_core simulation_crash test_control_simulacrum PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
