#pragma once

#if defined(__linux__) || defined(__linux)
    #define DSP_PLATFORM_LINUX 1
#endif

#if defined(_WIN32) || defined(_WIN64)
    #define DSP_PLATFORM_WINDOWS 1
#endif