# DSPA Master Roadmap: v1.0 & v2.0

## Vision 1.0: GPU Stability, Security & Mixing
**Goal:** Rock-solid foundation for professional mixing.
- **Hardware:** High-performance GPU (Vulkan).
- **Security:** Hardened isolation via `memfd` sealing and `Landlock`.
- **Latency:** Optimized for mixing (Target: 128-256 samples).

## Vision 2.0: NPU, FPGA, Specialized DSP Arrays & Zero-Latency Tracking
**Goal:** Sub-millisecond latency for live tracking on heterogeneous hardware.
- **Hardware:** NPU (OpenVINO/TensorRT), FPGA backends, and Massive DSP Arrays (PCIe/External).
- **Latency:** Optimized for live monitoring (Target: 16-32 samples). <-- NEXT FOCUS after v1.0 stability
- **Data Path:** Direct DMA/Peer-to-Peer memory access.
- **Processing:** Parallel Array computing for linear algebra-heavy DSP.

---
## I. IPC & Memory Foundation (15 Items)
1. [x] Implement `memfd_create` sealing verification in SDK to prevent unauthorized SHM modification.
2. [ ] Add `static_assert` verification for all IPC structure sizes across different GCC versions.
3. [ ] Implement a "Garbage Collector" for orphan `memfd` segments after a crash.
4. [x] Validate cache-line alignment (64-byte) for every member of `DspSharedMemory`.
5. [ ] Add a version-handshake in SHM preamble to prevent SDK/Worker mismatch.
6. [ ] Implement a "Safe Copy" fallback mode when `mmap` fails.
7. [x] Optimize `ShmSPSCRingBuffer` using bitwise operations for index wrapping.
8. [ ] Add an "XRUN" counter in SHM readable by the Supervisor.
9. [ ] Implement a prioritized "Emergency Signal" channel in SHM for immediate hardware stop.
10. [ ] Support dynamic resizing of ring buffers based on host latency settings.
11. [ ] Implement NUMA-aware memory allocation for Multi-CPU socket servers.
12. [ ] Add HMAC signatures to SHM segments to verify data integrity (Optional/Security).
13. [ ] Benchmark IPC overhead vs standard `write()` syscalls.
14. [ ] Ensure `SCM_RIGHTS` FD passing is compliant with Wayland/PipeWire security contexts.
15. [ ] Implement a "ReadOnly" mirror of the `WorkerControlBlock` for monitoring tools.

## II. PipeWire Supervisor & Spawner (20 Items)
16. [ ] **FIX**: Automatically detect and fix permissions for `/tmp/dsp_accel_daemon.sock`.
17. [ ] Implement an automatic cleanup of stale socket files on module load.
18. [ ] Replace `execl` with a robust process manager that captures worker `stderr`.
19. [ ] Implement a "Backoff Strategy" for worker restarts (to avoid infinite crash loops).
20. [ ] Add support for module arguments in `pw-cli load-module` (e.g., `worker_path=...`).
21. [ ] Implement true "Load Balancing": Route new plugins to the GPU with the lowest `load_pct`.
22. [ ] Integrate with `systemd-journald` for professional logging.
23. [ ] Add a "Dry Run" mode to verify Vulkan capability before spawning workers.
24. [ ] Implement a "Global Bypass" switch in the supervisor.
25. [ ] Add a JSON-RPC interface for remote monitoring via the Logging Bus.
26. [ ] Implement `cgroups` integration to limit worker CPU/Memory usage.
27. [ ] Automatically set `CAP_SYS_NICE` on worker binaries during installation.
28. [ ] Implement a "Watchdog Timeout" configurable via PipeWire properties.
29. [ ] Handle `SIGTERM` gracefully to close all worker processes.
30. [x] Implement a "Worker Heartbeat" monitor in SDK for automatic CPU fallback.
31. [ ] Add support for "Dedicated Workers" (1 worker per plugin instance for max isolation).
32. [ ] Implement "Shared Workers" (Standard multi-instance mode).
33. [ ] Add a "Power Save" mode that kills idle workers after X minutes.
34. [ ] Verify Supervisor compatibility with `PipeWire 1.2+`.
35. [ ] Implement a lock-file mechanism to prevent multiple supervisor instances.

## III. Vulkan & Hardware Backends (20 Items)
36. [ ] Implement **Vulkan Pipeline Cache** to reduce shader load time to < 1ms.
37. [ ] Add support for `VK_KHR_external_memory_fd` on AMD and Intel GPUs.
38. [ ] Implement a **VRAM Sub-allocator** to manage thousands of small state buffers.
39. [ ] Add "Thermal Throttling" detection: Warn the supervisor if GPU temp is > 85°C.
40. [ ] Support for **Timeline Semaphores** for more granular GPU-CPU sync.
41. [ ] Implement a "Shader Fallback" (Basic Gain) if the custom SPIR-V fails to load.
42. [ ] Add support for `VK_EXT_global_priority_query` to verify real-time scheduling.
43. [ ] Implement **Multi-Queue Dispatch**: process Left and Right channels on different Compute Units.
44. [ ] Add an "Offline Render" mode for high-quality export (no real-time constraints).
45. [ ] Implement **Descriptor Indexing** for dynamic shader resource binding.
46. [ ] Optimize Push Constants layout for 1.0 stable API.
47. [ ] Implement a "Compute-to-Graphics" bridge for future visualizers.
48. [ ] Support for **Vulkan 1.3 Dynamic State** where available.
49. [ ] Add an OpenVINO backend for NPU-based AI noise reduction.
50. [ ] Implement a "Tensor Cores" path for NVIDIA RTX hardware.
51. [ ] Add a "Unified Memory" toggle for integrated GPUs (iGPUs).
52. [ ] Implement shader validation layers in "Debug" builds only.
53. [ ] Benchmark PCI-E transfer overhead vs Compute time.
54. [ ] Add a "Double Precision" (float64) path for high-end mastering plugins.
55. [ ] **v2.0 PREP**: Research FPGA kernel driver interface for DMA mapping.

## VII. Phase 2: The "Tracking" Revolution (v2.0 Specifics)
101. [ ] Implement **Zero-Copy Bypass** refined for 16-sample buffers.
102. [ ] Integrate **OpenVINO Backend** for real-time Neural Amp Modeling (NAM).
103. [ ] Add **FPGA Bitstream Loader** in the Supervisor for Xilinx/Alveo cards.
104. [ ] Implement **DSP Array Orchestrator** for multi-core PCIe accelerators and systolic arrays.
105. [ ] Support for **Direct DMA** between audio interface and GPU/NPU memory.
106. [ ] Hardware-based sample-rate conversion for NPU models.
107. [ ] "Tracking Mode" UI toggle to prioritize latency over power saving.
108. [ ] Sub-millisecond Jitter Buffer for Network DSP Nodes.
109. [ ] Automated Benchmarking for Data Loading and Array throughput.

## IV. SDK & Developer Experience (15 Items - Continued)
56. [ ] Finalize the stable C API for `dsp_accel_sdk.h`. (v1.0)
57. [ ] Implement `dsp_accel_sdk_get_latency()` with microsecond precision.
58. [ ] Add a "Diagnostic Callback" for plugins to receive hardware error logs.
59. [ ] Implement automatic "Stall Detection" in the SDK dispatch loop.
60. [ ] Add a `DSPA_VERBOSE` environment variable for deep SDK tracing.
61. [ ] Create a `libdsp_accel.so` shared library with proper versioning.
62. [ ] Provide a CMake `find_package(DSPA)` module for third-party developers.
63. [ ] Implement a "Virtual Hardware" mode for CI testing without a GPU.
64. [ ] Add a `dsp_accel_sdk_reset()` function to clear all SHM queues.
65. [ ] Implement a "Latency Histogram" tool in the SDK.
66. [ ] Add support for "Context Sharing" between multiple plugins from the same vendor.
67. [ ] Implement thread-local storage for SDK contexts to improve thread-safety.
68. [ ] Provide a "Header-Only" version of the SDK for simple integrations.
69. [ ] Add a "Parameter Smoothing" helper in the SDK.
70. [ ] Document the "Thread Safety Guarantee" for all SDK functions.

## V. DSP Algorithms & Optimization (15 Items)
71. [ ] Implement a high-performance **FFT Engine** in SPIR-V.
72. [ ] Add a **Convolution Stage** for zero-latency IR processing.
73. [ ] Implement a **Stereo Biquad Filter** with stable coefficient updates.
74. [ ] Optimize buffer interleaving using **AVX-512** instructions.
75. [ ] Implement a "Soft-Knee Limiter" direct in the Vulkan stage.
76. [ ] Add a **Resampler Stage** (e.g., 44.1kHz to 48kHz) using GPU interpolation.
77. [ ] Implement a **Dither Engine** for 24-bit/16-bit output.
78. [ ] Optimize memory access patterns using **LDS (Local Data Store)** in Vulkan.
79. [ ] Implement a **Stateful Delay Line** with sub-sample interpolation.
80. [ ] Add support for **Sidechain Inputs** via a second IPC channel.
81. [ ] Implement a "Spectral Processor" template.
82. [ ] Optimize tanh/exp functions using GPU hardware look-up tables.
83. [ ] Add a "Multi-band Splitter" (Linkwitz-Riley) algorithm.
84. [ ] Implement a **Phase-Linear EQ** stage.
85. [ ] Benchmark total system latency from Plugin-In to Plugin-Out.

## VI. QA, Testing & Deployment (15 Items)
86. [ ] Implement a "Fuzz Tester" for the Unix Socket handshake.
87. [ ] Create a "GPU Hang Simulation" to verify Watchdog recovery.
88. [ ] Build a "Jitter Analysis" tool for the Network Node.
89. [x] Automated CI/CD pipeline on GitHub Actions (Initial build configuration).
90. [ ] Implement a "Static Analysis" step (Clang-Tidy / Cppcheck).
91. [ ] Create a **DSPA-CLI** utility for real-time system management.
92. [ ] Professional documentation site (Doxygen + MkDocs).
93. [ ] Provide a `deb` and `rpm` package for easy installation.
94. [ ] Implement a "Self-Update" mechanism for the worker binaries.
95. [ ] Verify low-latency performance on **Raspberry Pi 5** (NPU/GPU).
96. [ ] Add "Bit-Perfect" verification tests for all DSP modes.
97. [ ] Implement a "Stress Test" that spawns 64 concurrent plugin instances.
98. [ ] Document "WSL2 GPU Optimization" best practices.
99. [ ] Create a "Community Showcase" of plugins using DSPA.
100. [ ] Final Audit: 100% test coverage for the IPC and SDK core.