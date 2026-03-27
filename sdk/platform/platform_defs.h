#pragma once
/**
 * DSP Acceleration System - Platform Abstraction Layer
 *
 * This header detects and normalizes platform differences so the SDK
 * and all wrappers compile transparently on all supported targets:
 *   - Linux x86_64 (Primary: PipeWire + Vulkan)
 *   - Linux ARM64 (e.g. Raspberry Pi 5, Apple M1 under Asahi Linux)
 *   - Android (ARM64: direct Vulkan + AAudio IPC)
 *   - iOS (future: Metal Compute via abstraction shim)
 *   - Windows (future: WASAPI + D3D12 compute, no PipeWire)
 */

#pragma once

// ─── OS Detection ─────────────────────────────────────────────────
#if defined(__linux__) && defined(__ANDROID__)
    #define DSP_PLATFORM_ANDROID 1
    #define DSP_PLATFORM_NAME "Android/ARM"

#elif defined(__linux__)
    #define DSP_PLATFORM_LINUX 1
    #if defined(__aarch64__) || defined(__arm__)
        #define DSP_PLATFORM_NAME "Linux/ARM64"
        #define DSP_HAS_PIPEWIRE 1
        #define DSP_HAS_VULKAN   1
    #else
        #define DSP_PLATFORM_NAME "Linux/x86_64"
        #define DSP_HAS_PIPEWIRE 1
        #define DSP_HAS_VULKAN   1
    #endif

#elif defined(_WIN32)
    #define DSP_PLATFORM_WINDOWS 1
    #define DSP_PLATFORM_NAME "Windows"
    // Future: map to WASAPI + DirectML/D3D12 Compute
    #define DSP_HAS_PIPEWIRE 0
    #define DSP_HAS_VULKAN   1

#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        #define DSP_PLATFORM_IOS 1
        #define DSP_PLATFORM_NAME "iOS/Metal"
        #define DSP_HAS_PIPEWIRE 0
        #define DSP_HAS_VULKAN   0  // future: Metal Compute shim
    #else
        #define DSP_PLATFORM_MACOS 1
        #define DSP_PLATFORM_NAME "macOS/Metal"
        #define DSP_HAS_PIPEWIRE 0
        #define DSP_HAS_VULKAN   0
    #endif
#endif

// ─── IPC Mechanism per Platform ───────────────────────────────────
#if defined(DSP_PLATFORM_LINUX) || defined(DSP_PLATFORM_ANDROID)
    // Linux/Android: memfd_create + POSIX eventfd for zero-copy IPC
    #define DSP_IPC_MEMFD   1
    #define DSP_IPC_BACKEND "memfd + eventfd"
#else
    // Fallback: named pipes or TCP loopback
    #define DSP_IPC_PIPE    1
    #define DSP_IPC_BACKEND "named_pipe"
#endif

// ─── Compiler Hints ───────────────────────────────────────────────
#if defined(__GNUC__) || defined(__clang__)
    #define DSP_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define DSP_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define DSP_FORCE_INLINE __attribute__((always_inline)) inline
#else
    #define DSP_LIKELY(x)   (x)
    #define DSP_UNLIKELY(x) (x)
    #define DSP_FORCE_INLINE inline
#endif
