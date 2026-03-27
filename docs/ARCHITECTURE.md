# Architecture Deep Dive

This document explains the technical inner workings of the Linux DSP Acceleration system.

## 1. The Supervisor-Worker Model

To prevent system-wide audio crashes, we use a process-isolation strategy.

### The Supervisor (Proxy)
Located in `daemon/pipewire/module-dsp-accel.cpp`, the Supervisor runs inside the PipeWire process but performs **no hardware contact**. Its roles are:
- Creating `memfd` shared memory segments.
- Spawning Worker processes.
- Routing workloads based on metadata.
- Monitoring heartbeats.

### The Worker
Located in `daemon/worker.cpp`, each worker is a separate PID.
- It receives the Shared Memory File Descriptor (FD) via a Unix Domain Socket.
- It initializes the specific hardware backend (Vulkan, NPU, etc.).
- It runs a high-priority processing loop.
- If it crashes, it only takes down its own process, not PipeWire.

## 2. Zero-Latency IPC

Total isolation often comes with a latency penalty. We minimize this using:

- **Anonymous Shared Memory (`memfd`)**: Audio buffers never touch the disk.
- **FD Passing**: The Supervisor passes the `memfd` to the Worker via `SCM_RIGHTS`.
- **Lock-Free Queues**: We use a Single-Producer Single-Consumer (SPSC) ring buffer implemented in `daemon/ipc/include/ipc_shm.hpp`. It uses atomic `acquire/release` semantics to ensure that the CPU and GPU/NPU never wait for a lock.

## 3. The Control Block

The `WorkerControlBlock` is a 64-byte structure (cache-line aligned) that resides in the preamble of the shared memory:
- **Heartbeat**: A 64-bit counter incremented by the worker.
- **Error Register**: An enum used to communicate hardware-specific failures like `VK_DEVICE_LOST`.
- **Instruction Bits**: Allows the Supervisor to signal clean shutdowns or restarts.

### 3.1 The Memory Management Bus
To support non-blocking VRAM allocation, a secondary IPC bus has been added:
- **`memory_request_bus`**: The plugin (producer) sends `ALLOCATE`, `FREE`, or `UPLOAD` requests.
- **`memory_response_bus`**: The worker (producer) returns opaque hardware handles and success status.
- **Data Staging Area**: A fixed **4MB buffer** in the main SHM preamble used for high-bandwidth data transfers from CPU to GPU/NPU.

### 3.2 Zero-Copy Bypass (Low Latency Tracking)
For time-critical tracking, the system supports a **Zero-Copy Bypass**:
- **Mechanism**: The `memfd` of the PipeWire shared memory is imported directly into the Vulkan backend using `VK_KHR_external_memory_fd`.
- **Optimization**: This eliminates the `memcpy` overhead in the Worker process. The GPU reads audio frames directly from the SHM address space.
- **Dynamic Handshake**: During connection, the Supervisor sends the assigned `worker_id` to the SDK, allowing the plugin to target the correct control flags in the `WorkerControlBlock`.
When a CLAP plugin requests acceleration, it provides a `DspWorkloadType`. The Supervisor's `handle_plugin_request` function uses a scoring algorithm to find the best healthy **DSP Node** within a worker:
- **`DSP_TYPE_MASSIVELY_PARALLEL`** -> Routed to Vulkan/GPU Node.
- **`DSP_TYPE_TENSOR_ML`** -> Routed to NPU Node.
- **`DSP_TYPE_FIXED_POINT`** -> Routed to DSP Array Node.

## 5. The DSP Node Graph

To support complex heterogeneous offloading, we organize hardware interaction into a **Directed Acyclic Graph (DAG)** of DSP Nodes.

### Heterogeneous Staging
A single audio buffer can traverse multiple nodes before returning to the CPU:
1. **Node A (Vulkan)**: Massive FFT calculation.
2. **Node B (NPU)**: Neural noise reduction on the frequency domain.
3. **Node C (Vulkan)**: Inverse FFT and final smoothing.

### 5.1 VRAM Data Staging Area
For large datasets (Convolution Impulse Responses, Wavetables, or Samples), the system avoids per-frame CPU-to-GPU copies. Instead:
- Data is written once to the `data_staging` area.
- An `UPLOAD` request is sent via the memory bus.
- The Worker maps the staging area directly into a `DEVICE_LOCAL` Vulkan buffer.
- The buffer remains **resident** in VRAM until an explicit `FREE` command or worker termination.

### 5.4 Dynamic Control over Parallelization
Hardware pipelines can be dynamically fragmented using the `prefer_block_parallel` flag in Shared Memory. The Worker evaluates this flag alongside real-time telemetry:
*   **Load Check**: If GPU load is > 60%, fragmentation is avoided to minimize dispatch overhead.
*   **Granularity**: Small blocks (< 128 samples) remain linear to maximize CPU/GPU cache efficiency.
*   **Plugin Request**: The plugin can disable fragmentation for strictly sequential algorithms that do not benefit from block splitting.

## 6. Real-Time Vulkan Modulation (Future)

To achieve the "On-the-Fly" behavior modulation requested, the system will leverage two core Vulkan mechanisms controlled via our **Control Bus**:

### 6.1 Low-Latency Push Constants
For continuous parameters (Gain, Filter Cutoff, Compressor Threshold), the Worker will use `vkCmdPushConstants`. 
- **Latency**: Sub-microsecond.
- **Workflow**: The Worker drains the `control_bus`, updates a small local buffer, and pushes it directly into the shader before each `vkCmdDispatch`.

### 6.2 Dynamic Descriptor Swapping
For structural changes (e.g., swapping a Convolution Impulse Response or a WaveTable), the Worker will use **Descriptor Set Swapping**:
- **Workflow**: The Worker pre-loads several states and "hot-swaps" the pointer in the descriptor set without re-creating the entire pipeline.

### 6.3 Shader Specialization (Optional)
While specialization constants are traditionally static, we may implement a "Pipeline Pool" where several variants of a shader coexist (e.g., Mono vs Stereo, or High-Quality vs Eco-Mode), and the Worker chooses the optimal one in real-time based on `control_bus` messages.

### 6.4 Non-Audio Data Compute
The system is now capable of processing arbitrary data streams via the `DSP_TYPE_ARRAY_PROCESSOR` workload. By using the `allocate_vram` and `upload_buffer` APIs, a plugin can offload raw mathematical operations (e.g., spectral analysis, video processing for visualizers) to the GPU/NPU. Dispatch your kernels and read back the results from the `out_queue`.

### 6.5 The Dual Latency Model

It is critical to distinguish between two types of latency in this system:

1.  **Modulation Latency (Zero Lag)**: Thanks to the hardware **Control Bus** and **Push Constants**, a parameter change (e.g., moving a knob) is applied to the very first audio block processed after the event is received. There is no perceived "lag" in control response.
2.  **Audio Throughput Latency (Fixed Delay)**: To ensure hardware stability, the system maintains a fixed **1-block round-trip delay** (e.g., 1.3ms at 64 frames/48kHz). This is standard for external DSP and is automatically compensated by the DAW's **Plugin Delay Compensation (PDC)** via the CLAP latency extension.

## 7. Vulkan Real-Time Scheduling

To mimic the behavior of a Linux RT-Kernel on the GPU, we implement a **Hardware Scheduling Layer**:

- **Real-Time Queues**: We request the `VK_EXT_global_priority` extension during device creation.
- **Priority Level**: We specify `VK_QUEUE_GLOBAL_PRIORITY_REALTIME_EXT`. This signals the hardware scheduler (on supporting drivers like AMD/Nvidia/Intel) to give our compute queue absolute precedence over graphics rendering or background GPGPU tasks.
- **Isolation**: By using a dedicated Compute Queue family, we ensure that audio processing is never blocked by "V-Sync" or other display-related synchronization events.
