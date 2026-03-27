# Developer & Contributor Guide

Welcome! This guide explains how to extend and contribute to the project.

## 1. Project Structure
- `/daemon/pipewire`: The Supervisor module (PipeWire integration).
- `/daemon/vulkan`: GPU compute implementation.
- `/daemon/ipc`: Key IPC structures and ring buffers.
- `/sdk`: The C SDK for audio plugins.
- `/mcp-server`: Node.js management bridge.
- `/tests`: Unit tests and crash simulations.

## 2. Adding a New Hardware Backend
To add support for a new hardware type (e.g., a custom DSP chip):
1. **Define the Type**: Add your new type to `sdk/include/dsp_accel_sdk.h`.
2. **Implement the Interface**: Create a new class in the daemon that inherits from `IDspNode` (see `compute_backend.hpp`).
3. **Update the Worker**: In `worker.cpp`, add a CLI flag to initialize your new backend.
4. **Register in Supervisor**: Update `spawn_worker` in the Proxy to support the new type.

## 3. Developing for the MCP Server
The MCP server is written in Node.js using the `@modelcontextprotocol/sdk`.
- To add a new tool: Update `setRequestHandler(ListToolsRequestSchema, ...)` in `mcp-server/index.js`.
- Tools are intended to be used by AI agents to help manage the system automatically.

## 4. Debugging
### Logging
We use the PipeWire logging system in the Supervisor:
```bash
export PIPEWIRE_DEBUG=3
pw-dump
```
In workers, we use standard `stderr` which is captured by the Supervisor.

### Crash Testing
Use the `simulation_crash` test to verify that your changes don't break the failover mechanism:
```bash
./run_tests.sh
```

## 5. Coding Standards
- **C++20**: Use RAII and modern atomics.
- **Lock-Free**: Never use `std::mutex` in the audio path.
- **Safety First**: Any hardware contact code MUST reside in a Worker process, never in the Supervisor.

## 6. Working with VRAM & Samples
To load persistent data (like Impulse Responses or Wavetables) into the accelerator:

1. **Allocate VRAM**: Use `dsp_accel_sdk_allocate_vram(ctx, size)`. This returns a `uint32_t` handle.
2. **Upload Data**: Copy your data to `ctx->shm->data_staging` and call `dsp_accel_sdk_upload_buffer(ctx, handle, data_staging_ptr, size)`.
3. **Use in Dispatch**: Your shader or NPU model can now access the buffer using the handle provided.
4. **Cleanup**: Call `dsp_accel_sdk_free_vram(ctx, handle)` during plugin destruction.

> [!TIP]
> Always perform allocations and uploads during the plugin's `prepare_to_play` or state-loading phase, as these operations require an asynchronous handshake with the daemon which can take up to 500ms.

## 7. Non-Audio Data Compute
To use the hardware for pure GPGPU tasks:
- Connect with `DSP_TYPE_ARRAY_PROCESSOR`.
- Use the VRAM API to upload your input buffers.
- Dispatch your kernels and read back the results from the `out_queue`.

## 8. Latency Optimization: Zero-Copy Mode
If your plugin is used for **live monitoring (tracking)**, you can further reduce latency by bypassing intermediate CPU copies:

1. **Enable Bypass**: Call `dsp_accel_sdk_set_zero_copy_bypass(ctx, true)`.
2. **Behavior**: The hardware worker will map the `memfd` directly into Vulkan resources.
3. **Toggle on-fly**: You can safely toggle this flag during playback. It is recommended to enable it during recording and disable it for final high-precision mixing (Safe Mode).

> [!IMPORTANT]
> Zero-Copy mode requires the Vulkan driver to support external memory extensions. If unsupported, the system automatically falls back to standard "Safe Copy" mode with 1-block latency.
