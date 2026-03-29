# System Architecture

The Linux Universal DSP Acceleration (DSPA) framework is designed to provide transparent, low-latency hardware acceleration for audio plugins.

## Core Components

### 1. The Supervisor (PipeWire Module)
Acting as the "Brain," the supervisor is a PipeWire module that:
- Manages the lifecycle of hardware worker processes.
- Listens for plugin connection requests on `/run/dsp_accel/daemon.sock`.
- Performs load balancing across available GPUs and NPUs.
- Monitors heartbeats and performs automatic restarts on worker failure.

### 2. The Worker Processes
Workers are isolated processes running with real-time priority (`SCHED_FIFO`). Each worker is specialized for a backend (Vulkan, NPU, or Remote) and communicates with plugins exclusively via lock-free Shared Memory (SHM).

### 3. The SDK
A lightweight C++ library that plugins link against. It handles the Unix Socket handshake to receive file descriptors from the Supervisor and provides a simple API for audio dispatch and parameter modulation.

## Data Flow

1. **Handshake**: Plugin connects to Supervisor via Unix Socket. Supervisor sends 3 FDs (SHM, Send EventFD, Recv EventFD).
2. **Mmap**: Plugin maps the SHM into its own address space.
3. **Process**: 
    - Plugin writes audio to `in_queue` and signals Send EventFD.
    - Worker wakes up, processes data (e.g., via Vulkan Compute), writes to `out_queue`, and signals Recv EventFD.
    - Plugin wakes up and reads processed audio.

## Isolation & Security
- **Sandboxing**: Workers can be sandboxed using seccomp and namespaces to prevent rogue DSP code from accessing the host filesystem.
- **Anonymous SHM**: The use of `memfd_create` ensures that audio data is never visible in `/dev/shm`, providing security between different plugin instances.