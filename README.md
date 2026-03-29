# Linux Universal DSP Acceleration (DSPA)

A high-performance, real-time framework for offloading audio DSP workloads from CLAP plugins to hardware accelerators (GPUs via Vulkan, NPUs, and Remote Hosts) using PipeWire as the supervisor.

## Architecture Highlights

- **Zero-Copy IPC**: Utilizes `memfd` and Single-Producer Single-Consumer (SPSC) lock-free ring buffers for ultra-low latency audio transfer between the plugin and the hardware worker.
- **Vulkan Compute Backend**: Harnesses GPU power for massively parallel DSP tasks (FFT, FIR, Additive Synthesis) with real-time hardware scheduling.
- **PipeWire Integration**: A supervisor module manages hardware worker lifecycles, load balancing, and dynamic routing.
- **Hardware-Agnostic SDK**: A simple C++ SDK allows plugins to connect to the acceleration infrastructure with automatic CPU fallback.
- **Resilience & Monitoring**: Integrated watchdog heartbeats and error signaling ensure the audio thread never hangs, even if hardware drivers fail.

## Components

1.  **Supervisor (PipeWire Module)**: Spawns and monitors workers, handles plugin connection requests, and performs load balancing.
2.  **Workers**: Independent processes (Vulkan, NPU, Remote) that execute the actual DSP algorithms with real-time priority (`SCHED_FIFO`).
3.  **SDK**: The interface used by plugins to communicate with the Supervisor and Workers.
4.  **Network Node**: Extends acceleration to external Linux machines via low-latency UDP with jitter buffering.

## Prerequisites

- **OS**: Linux (Ubuntu 24.04+ recommended) or WSL2 with GPU support.
- **Build Tools**: CMake 3.20+, Ninja, GCC/G++ 13+.
- **Libraries**: PipeWire 0.3+, Vulkan SDK, glslang-tools.

## Building

Run the environment setup script to install dependencies and clone the CLAP SDK:

```bash
./setup_linux_env.sh
```

Compile the project:

```bash
mkdir build && cd build
cmake -G Ninja ..
ninja
```

## Running

1.  **Load the Supervisor**:
    ```bash
    pw-cli load-module ./build/daemon/pipewire/module-dsp-acc-accel.so
    ```
2.  **Start a Hardware Worker**:
    ```bash
    ./build/daemon/pipewire/dsp-accel-worker --type vulkan --id 0
    ```
3.  **Run the Example**:
    ```bash
    ./build/examples/hello_world_plugin/hello_world_plugin
    ```

## Testing

The project includes a comprehensive suite of real-time safety and integrity tests:

- `test_audio_loopback`: Validates bit-perfect integrity of the IPC buffers.
- `test_control_simulacrum`: Verifies real-time parameter modulation.
- `simulation_crash`: Tests the supervisor's ability to handle worker failures.

## License
The SDK implementation is licensed under the **MIT License**. The core system and daemon are licensed under **GPLv3**.