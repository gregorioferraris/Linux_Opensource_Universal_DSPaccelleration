cmake_minimum_required(VERSION 3.20)
project(HelloWorldPlugin LANGUAGES C)

include_directories(../../sdk/include)

add_executable(hello_world_plugin main.c ../../sdk/src/dsp_accel_sdk.c)

# Note: In a real system, you would link against librt and math
target_link_libraries(hello_world_plugin PRIVATE)
