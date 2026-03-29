# IPC Protocol Specification (v1.0.0)

This document defines the binary layout of the shared memory segments and the synchronization primitives used for zero-copy audio transfer.

## Shared Memory Layout (`DspSharedMemory`)

| Offset | Type | Description |
|--------|------|-------------|
| 0x000  | `WorkerControlBlock[8]` | Health, load, and bypass toggles for workers. |
| 0x200  | `SPSC Queue (Input)` | 16-slot ring buffer for `DspAudioFrame`. |
| 0x...  | `SPSC Queue (Output)` | 16-slot ring buffer for `DspAudioFrame`. |
| 0x...  | `SPSC Queue (Control)` | High-priority parameter modulation events. |
| 0x...  | `SPSC Queue (Logging)` | Asynchronous hardware diagnostics. |
| 0x...  | `Data Staging` | 4MB buffer for large VRAM uploads (IRs, wavetables). |

### Audio Frame Layout
The `DspAudioFrame` now includes an `instance_id` field. This is critical for **State Persistence**: it allows the GPU backend to retrieve the specific delay lines or filter coefficients associated with a particular plugin instance.

## Synchronization Primitives

### 1. EventFD
We use `eventfd` instead of Mutexes or Condition Variables. This allows the audio thread to perform a non-blocking `poll()` with a strict timeout, ensuring the DAW remains responsive even if the hardware backend hangs.

### 2. Cache Line Alignment
All atomic headers and ring buffer pointers are aligned to **64 bytes** (`CACHE_LINE_SIZE`). This prevents "False Sharing," where the CPU wastes cycles synchronizing cache lines between the Plugin thread and the Worker thread.

## The Handshake (SCM_RIGHTS)

The Supervisor passes file descriptors to the Plugin and Worker using `sendmsg` with the `SCM_RIGHTS` control message. This is the most secure way to share memory on Linux, as the memory segment has no name in the filesystem and is only accessible by processes that hold the FD.

## Network Packet Format

For `RemoteDspNode`, data is encapsulated in UDP packets with a `NetPacketHeader`:
- **Magic**: `0x44535041` ('DSPA')
- **Sequence ID**: 32-bit counter for jitter buffer reordering.
- **Timestamp**: 64-bit nanosecond clock for RTT measurement.
- **Payload**: RAW `DspAudioFrame`.