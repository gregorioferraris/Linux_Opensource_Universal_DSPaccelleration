#pragma once
/**
 * DSP Acceleration System — Real-Time Monitor API
 *
 * Provides two consumer interfaces:
 *   1. C API: embed in the daemon to publish metrics to shared memory
 *   2. Observer pattern: external TUI/GUI tools read metrics non-invasively
 *
 * Design: Zero-cost when no observer is attached (a single atomic check).
 * Metrics are updated per-audio-block from the RT thread.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── Per-Node Metrics (one per GPU/NPU/CPU node) ──────────────────
typedef struct {
    char     node_name[64];         // e.g. "NVIDIA RTX 3060", "Intel Xe", "CPU"
    atomic_uint xrun_count;         // IPC ring-buffer overruns (underruns on pop)
    atomic_uint dispatched_blocks;  // total blocks processed since start
    float    load_percent;          // 0-100 load on this node
    float    avg_roundtrip_us;      // rolling average round-trip in microseconds
    float    peak_roundtrip_us;     // worst case since last reset
    bool     is_active;             // is this node currently processing?
} DspMonitorNodeStats;

// ─── Global Monitor Context ───────────────────────────────────────
#define DSP_MONITOR_MAX_NODES 16

typedef struct {
    uint32_t            num_nodes;
    DspMonitorNodeStats nodes[DSP_MONITOR_MAX_NODES];
    atomic_uint         total_plugins_routed;
    uint64_t            uptime_ms;
} DspMonitorContext;

/**
 * @brief Initialize a monitor context. Call once at daemon startup.
 *        The context lives in a memfd segment so external tools can read it.
 */
DspMonitorContext* dsp_monitor_init(int num_nodes);

/**
 * @brief Update a single node's round-trip time. Called from the RT thread.
 * Uses only atomics — NEVER locks. Safe to call from the audio dispatch path.
 *
 * @param ctx       The monitor context.
 * @param node_idx  Index of the compute node being reported.
 * @param roundtrip_us  Measured round-trip time in microseconds.
 */
void dsp_monitor_update_rt(DspMonitorContext* ctx, int node_idx, float roundtrip_us);

/**
 * @brief Record an XRUN (buffer overrun) on a specific node.
 * Increments the xrun_count atomically.
 */
void dsp_monitor_record_xrun(DspMonitorContext* ctx, int node_idx);

/**
 * @brief Reset peak stats. Can be called from a non-RT management thread.
 */
void dsp_monitor_reset_peaks(DspMonitorContext* ctx);

/**
 * @brief Take a read-only snapshot of all stats. Thread-safe.
 * External TUI/GUI tools (e.g. a `dsp-accel-monitor` CLI) call this.
 */
bool dsp_monitor_snapshot(const DspMonitorContext* ctx, DspMonitorContext* out_snapshot);

/**
 * @brief Destroy the monitor context and release memory.
 */
void dsp_monitor_destroy(DspMonitorContext* ctx);

// ─── Golden Test Harness ──────────────────────────────────────────
/**
 * @brief Run a correctness golden test for a specific compute node.
 * Generates an input signal, runs it through the CPU path and the
 * hardware path, and compares sample-by-sample within a tolerance.
 * This ensures floating-point differences between GPU and CPU are acceptable.
 *
 * @param node_idx    Node to test.
 * @param tolerance   Max acceptable difference per sample (e.g. 1e-4f).
 * @return true if all samples are within tolerance, false on mismatch.
 */
bool dsp_monitor_run_golden_test(int node_idx, float tolerance);

#ifdef __cplusplus
}
#endif
