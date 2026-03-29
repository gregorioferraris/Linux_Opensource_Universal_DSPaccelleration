#pragma once
/**
 * DSP Acceleration System — Plugin Sandboxing API
 *
 * Provides isolation for untrusted / third-party plugin code running
 * inside the DSP daemon process. Uses Linux-only primitives:
 *   - seccomp-BPF: restrict which syscalls are allowed
 *   - Linux namespaces: isolate filesystem, network, and IPC views
 *   - memfd_create:  anonymous IPC memory (auto-freed on process death)
 *
 * NOTE: On non-Linux platforms this header no-ops gracefully.
 */

#include "../../../sdk/platform/platform_defs.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef DSP_PLATFORM_LINUX

#include <sys/types.h>
#include <unistd.h>

// ─── Sandbox Profile ──────────────────────────────────────────────
typedef enum {
    SANDBOX_PROFILE_STRICT   = 0,  // Minimal syscalls: audio IPC + compute only
    SANDBOX_PROFILE_STANDARD = 1,  // Adds file open (read-only /proc) for diagnostics
    SANDBOX_PROFILE_NONE     = 2   // Disabled (development/debug only)
} DspSandboxProfile;

typedef struct dsp_sandbox_ctx dsp_sandbox_ctx_t;

/**
 * @brief Enter a sandbox context before loading / running plugin code.
 * Creates a new seccomp filter and optionally a new namespace.
 *
 * @param profile  Syscall allowlist profile to apply.
 * @param pid      0 = current process, else target PID.
 * @return Opaque sandbox context or NULL on failure.
 */
dsp_sandbox_ctx_t* dsp_sandbox_enter(DspSandboxProfile profile, pid_t pid);

/**
 * @brief Check whether the current process is running inside a sandbox.
 */
bool dsp_sandbox_is_active(dsp_sandbox_ctx_t* ctx);

/**
 * @brief Create an anonymous shared memory segment using memfd_create.
 * The FD is inheritable across fork/exec via SCM_RIGHTS but NOT visible
 * in /dev/shm, so it cannot be accessed or corrupted by other processes.
 *
 * @param name     Human-readable label for debugging.
 * @param size     Size in bytes of the memory region.
 * @return File descriptor or -1 on error.
 */
int dsp_sandbox_create_shm(const char* name, size_t size);

/**
 * @brief Pass a memfd file descriptor securely to another process via
 * a UNIX domain socket (SCM_RIGHTS). This is the only way to share
 * anonymous memory between the plugin and the daemon.
 *
 * @param socket_fd  Connected UNIX socket to the receiver.
 * @param mem_fd     The memfd to transfer.
 */
bool dsp_sandbox_send_shm(int socket_fd, int mem_fd);

/**
 * @brief Exit and clean up the sandbox context.
 * All memfds are automatically released by the kernel when the process exits,
 * even if the plugin crashes (SIGKILL). No memory leaks possible.
 */
void dsp_sandbox_destroy(dsp_sandbox_ctx_t* ctx);

#else // Non-Linux stubs

typedef struct {} dsp_sandbox_ctx_t;
static inline dsp_sandbox_ctx_t* dsp_sandbox_enter(int p, int pid) { return (dsp_sandbox_ctx_t*)1; }
static inline int  dsp_sandbox_is_active(dsp_sandbox_ctx_t* c) { return 0; }
static inline int  dsp_sandbox_create_shm(const char* n, unsigned long s) { return -1; }
static inline int  dsp_sandbox_send_shm(int s, int m) { return 0; }
static inline void dsp_sandbox_destroy(dsp_sandbox_ctx_t* c) {}

#endif // DSP_PLATFORM_LINUX
