#pragma once
#include "compute_backend.hpp"
#include "ipc_shm.hpp"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <thread>
#include <map>
#include <chrono>
#include <atomic>

namespace DspAccel {
namespace Daemon {

/**
 * @brief Nodo DSP bidirezionale per comunicazione tra host e controller esterni.
 */
class RemoteDspNode : public IDspNode {
    int socket_fd;
    struct sockaddr_in remote_addr;
    uint32_t seq_id = 0;
    std::atomic<bool> active{false};
    std::thread rx_thread;
    Ipc::DspSharedMemory* shm_ptr_ = nullptr;

    // Jitter Buffer logic
    uint32_t expected_seq_id = 0;
    std::map<uint32_t, Ipc::DspAudioFrame> reorder_buffer;

    // Meccanismo di clock sync
    int64_t clock_offset_ns = 0;
    uint32_t current_sample_rate = 48000;

public:
    RemoteDspNode(const char* remote_ip, int port, uint32_t sample_rate) 
        : current_sample_rate(sample_rate) {
        socket_fd = socket(AF_INET, SOCK_DGRAM, 0); // UDP per bassa latenza
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_port = htons(port);
        inet_pton(AF_INET, remote_ip, &remote_addr.sin_addr);
    }

    ~RemoteDspNode() {
        active = false;
        if (rx_thread.joinable()) rx_thread.join();
        if (socket_fd >= 0) close(socket_fd);
    }

    bool init(int device_index) override {
        if (socket_fd < 0) return false;
        active = true;
        
        // Avvia il thread di ricezione (per gestire il ritorno dell'audio elaborato)
        rx_thread = std::thread(&RemoteDspNode::receive_loop, this);
        
        // Invia un pacchetto di Handshake iniziale
        send_packet(Ipc::Network::PacketType::HANDSHAKE, nullptr, 0);
        return true;
    }

    bool process_stage(Ipc::DspAudioFrame& frame) override {
        // Invia il frame all'host esterno con timestamp del clock locale
        return send_packet(Ipc::Network::PacketType::AUDIO_FRAME, &frame, sizeof(frame));
    }

    bool send_packet(Ipc::Network::PacketType type, const void* payload, size_t size) {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        Ipc::Network::NetPacketHeader header {
            0x44535041,
            type,
            seq_id++,
            (uint32_t)size,
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(now).count(),
            current_sample_rate
        };

        struct iovec iov[2];
        iov[0].iov_base = &header;
        iov[0].iov_len = sizeof(header);
        iov[1].iov_base = (void*)payload;
        iov[1].iov_len = size;

        struct msghdr msg = {};
        msg.msg_name = &remote_addr;
        msg.msg_namelen = sizeof(remote_addr);
        msg.msg_iov = iov;
        msg.msg_iovlen = (payload ? 2 : 1);

        return sendmsg(socket_fd, &msg, 0) > 0;
    }

    void receive_loop() {
        std::vector<uint8_t> receive_buf(sizeof(Ipc::Network::NetPacketHeader) + sizeof(Ipc::DspAudioFrame));
        while (active) {
            ssize_t n = recv(socket_fd, receive_buf.data(), receive_buf.size(), 0);
            if (n < (ssize_t)sizeof(Ipc::Network::NetPacketHeader)) continue;

            auto* header = reinterpret_cast<Ipc::Network::NetPacketHeader*>(receive_buf.data());
            if (header->magic != 0x44535041) continue;

            if (header->type == Ipc::Network::PacketType::AUDIO_FRAME && shm_ptr_) {
                // Logica Jitter Buffer: inserimento ordinato
                if (header->sequence_id >= expected_seq_id) {
                    auto& frame_slot = reorder_buffer[header->sequence_id];
                    memcpy(&frame_slot, 
                           receive_buf.data() + sizeof(Ipc::Network::NetPacketHeader), 
                           sizeof(Ipc::DspAudioFrame));
                }

                // Svuota i pacchetti pronti in sequenza
                while (reorder_buffer.count(expected_seq_id)) {
                    if (!shm_ptr_->out_queue.push(reorder_buffer[expected_seq_id])) {
                        // SHM Saturation
                    }
                    reorder_buffer.erase(expected_seq_id);
                    expected_seq_id++;
                }

                // Pulizia buffer se troppo grande (pacchetti persi)
                if (reorder_buffer.size() > 16) {
                    expected_seq_id = reorder_buffer.begin()->first;
                }
            } else if (header->type == Ipc::Network::PacketType::CLOCK_SYNC) {
                // Calcolo offset temporale tra le due macchine Linux
                auto now = std::chrono::steady_clock::now().time_since_epoch().count();
                clock_offset_ns = (int64_t)header->timestamp_ns - now;
            }
        }
    }

    void apply_control_event(uint32_t param_id, float value) override {
        Ipc::DspControlEvent evt { param_id, value };
        send_packet(Ipc::Network::PacketType::CONTROL_EVENT, &evt, sizeof(evt));
    }

    // Metodi stub per la gestione memoria (l'host esterno gestisce la sua VRAM)
    uint32_t allocate_buffer(size_t s) override { return 0; }
    bool upload_buffer(uint32_t h, const void* d, size_t s) override { return true; }
    void free_buffer(uint32_t h) override {}
    void set_zero_copy_bypass(bool e, int f, size_t s) override {}
    void set_shm_ptr(void* p) override { shm_ptr_ = (Ipc::DspSharedMemory*)p; }
    DspNodeDescriptor get_descriptor() const override { return {"External_Host", DSP_TYPE_REMOTE, 1024, false}; }
};
}}