#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace DspAccel {
namespace Ipc { // Root namespace for Inter-Process Communication

/**
 * @brief Network protocol for external DSP host communication (v2.0 Ready).
 */
namespace Network {
    enum class PacketType : uint32_t {
        HANDSHAKE = 0,
        AUDIO_FRAME = 1,
        CONTROL_EVENT = 2,
        KEEPALIVE = 3,
        CLOCK_SYNC = 4  // Pacchetto per calcolare RTT e clock offset
    };

    struct NetPacketHeader {
        uint32_t magic;         // Identifier 'DSPA' (0x44535041)
        PacketType type;
        uint32_t sequence_id;   // Per gestire l'ordine dei pacchetti UDP
        uint32_t payload_size;
        uint64_t timestamp_ns;  // Tempo di invio (CLOCKS_MONOTONIC)
        uint32_t sample_rate;   // Frequenza di campionamento corrente
        uint32_t reserved;      // Allineamento
    };
}

// Cache line size to avoid false sharing
constexpr size_t CACHE_LINE_SIZE = 64;

// Maximum latency target for v1.0 (Mixing): 256 samples.
// v2.0 (Tracking) will target smaller buffers (16-32).

// Fix #5: DspAudioFrame supports up to 256 frames per block, 8 channels.
// frame_count and channel_count indicate how many are valid.
struct DspAudioFrame {
    float    samples[256 * 8]; // max 256 frames * 8 channels
    uint32_t frame_count;      // actual number of frames in this block
    uint32_t channel_count;    // actual number of channels
};
static_assert(std::is_trivially_copyable_v<DspAudioFrame>);

/**
 * @brief Lock-free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 * Designed to be placed in Shared Memory (POSIX shm).
 */
template <typename T, size_t Capacity>
class ShmSPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two for performance");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for shared memory");
    static_assert(std::atomic<size_t>::is_always_lock_free, "std::atomic<size_t> must be lock-free for RT audio safe IPC");

    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
    alignas(CACHE_LINE_SIZE) T data_[Capacity];

public:
    ShmSPSCRingBuffer() = default;
    ShmSPSCRingBuffer(const ShmSPSCRingBuffer&) = delete;
    ShmSPSCRingBuffer& operator=(const ShmSPSCRingBuffer&) = delete;

    bool push(const T& item) {
        auto current_tail = tail_.load(std::memory_order_relaxed);
        auto next_tail = (current_tail + 1) & (Capacity - 1);
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue is full -> XRUN
        }
        data_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    bool pop(T& out_item) {
        auto current_head = head_.load(std::memory_order_relaxed);
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue is empty
        }
        out_item = data_[current_head];
        head_.store((current_head + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    size_t capacity() const { return Capacity; }
};

// Fix #2 & #3: The full shared memory layout sent by the daemon via memfd.
// The daemon creates this struct in a memfd and maps it into both processes.
// - in_queue:  plugin (producer) → daemon (consumer)
// - out_queue: daemon (producer) → plugin (consumer)
constexpr size_t IPC_QUEUE_DEPTH = 16; // 16 blocks buffered in each direction

enum class WorkerError : uint32_t {
    NONE = 0,
    DEVICE_LOST = 1,
    OUT_OF_MEMORY = 2,
    PERMISSION_DENIED = 3,
    INTERNAL_ERROR = 4
};

// Control block for a single hardware worker process
struct WorkerControlBlock {
    std::atomic<uint64_t> heartbeat;       // Incremented by worker
    std::atomic<WorkerError> last_error;   // Set by worker on failure
    std::atomic<bool>     is_ready;        // Set by worker when init is complete
    std::atomic<bool>     should_restart;  // Set by supervisor to request restart
    std::atomic<bool>     bypass_zero_copy; // Toggle for direct SHM-GPU mapping
    std::atomic<uint32_t> load_pct;        // 0-100 current GPU/HW load
    uint8_t               padding[44];      // Align to 64-byte cache line
};
static_assert(sizeof(WorkerControlBlock) == CACHE_LINE_SIZE);

/**
 * @brief Dynamic Control Event for real-time hardware modulation.
 */
struct DspControlEvent {
    uint32_t parameter_id; // e.g., GAIN, CUTOFF, POWER_PROFILE
    float    value;        // normalized or literal value
};
static_assert(std::is_trivially_copyable_v<DspControlEvent>);

/**
 * @brief Memory Management Request for VRAM allocations/uploads.
 */
enum class DspMemoryRequestType : uint32_t {
    ALLOCATE = 0,
    FREE     = 1,
    UPLOAD   = 2
};

struct DspMemoryRequest {
    uint32_t            request_id;
    DspMemoryRequestType type;
    size_t              size;
    uint32_t            handle;     // Used for FREE and UPLOAD
    uint64_t            shm_offset; // Offset in collateral memfd for UPLOAD data
};
static_assert(std::is_trivially_copyable_v<DspMemoryRequest>);

struct DspMemoryResponse {
    uint32_t request_id;
    uint32_t handle;    // Allocated handle or error code
    bool     success;
};
static_assert(std::is_trivially_copyable_v<DspMemoryResponse>);

struct DspSharedMemory { // Main SHM structure for v1.0 Security
    WorkerControlBlock workers[8]; // Support up to 8 independent hardware workers
    ShmSPSCRingBuffer<DspAudioFrame, IPC_QUEUE_DEPTH> in_queue;   // plugin  → daemon
    ShmSPSCRingBuffer<DspAudioFrame, IPC_QUEUE_DEPTH> out_queue;  // daemon  → plugin
    
    // Control Bus: plugin → daemon (Real-time parameter modulation)
    ShmSPSCRingBuffer<DspControlEvent, 64> control_bus;
    
    // Memory Management Bus: plugin → daemon (Alloc/Free/Upload)
    ShmSPSCRingBuffer<DspMemoryRequest, 32> memory_request_bus;

    // Memory Response Bus: daemon → plugin (Return handles/errors)
    ShmSPSCRingBuffer<DspMemoryResponse, 32> memory_response_bus;

    // Staging area for VRAM uploads (4MB)
    alignas(CACHE_LINE_SIZE) uint8_t data_staging[1024 * 1024 * 4];

    std::atomic<bool> prefer_block_parallel; // Plugin toggle for GPU fragmentation
    uint8_t           padding[63];           // Align structure end to 64-byte cache line
};

} // namespace Ipc
} // namespace DspAccel
