#pragma once
#include "compute_backend.hpp"
#include "../Linux_DSP_accelleration.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace DspAccel {
namespace Daemon {

/**
 * @brief Server DSP Remoto: Riceve audio via rete, lo elabora localmente e lo rispedisce.
 * Questo è il modulo "specchiato" da eseguire sull'hardware acceleratore esterno.
 */
class NetworkServerNode {
    int socket_fd;
    IDspNode* local_worker; // Es. VulkanBackend o NpuBackend
    bool running = false;

public:
    NetworkServerNode(IDspNode* worker) : local_worker(worker) {
        socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    }

    ~NetworkServerNode() { if (socket_fd >= 0) close(socket_fd); }

    void start(int port) {
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return;

        running = true;
        uint8_t buffer[sizeof(Ipc::Network::NetPacketHeader) + sizeof(Ipc::DspAudioFrame)];
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        while (running) {
            ssize_t n = recvfrom(socket_fd, buffer, sizeof(buffer), 0, 
                                 (struct sockaddr*)&client_addr, &client_len);
            
            if (n < (ssize_t)sizeof(Ipc::Network::NetPacketHeader)) continue;

            auto* header = (Ipc::Network::NetPacketHeader*)buffer;
            if (header->magic != 0x44535041) continue;

            if (header->type == Ipc::Network::PacketType::AUDIO_FRAME) {
                Ipc::DspAudioFrame* frame = (Ipc::DspAudioFrame*)(buffer + sizeof(Ipc::Network::NetPacketHeader));
                
                // 1. Elaborazione locale (Vulkan/NPU)
                local_worker->process_stage(*frame);

                // 2. Rispedisci il frame elaborato al mittente
                // Manteniamo lo stesso sequence_id per permettere all'host di ricollegarlo
                header->timestamp_ns = std::chrono::steady_clock::now().time_since_epoch().count();
                
                struct iovec iov[2];
                iov[0].iov_base = header;
                iov[0].iov_len = sizeof(*header);
                iov[1].iov_base = frame;
                iov[1].iov_len = sizeof(*frame);

                struct msghdr msg = {};
                msg.msg_name = &client_addr;
                msg.msg_namelen = client_len;
                msg.msg_iov = iov;
                msg.msg_iovlen = 2;

                sendmsg(socket_fd, &msg, 0);

            } else if (header->type == Ipc::Network::PacketType::HANDSHAKE) {
                // Risposta all'handshake per confermare disponibilità
                header->type = Ipc::Network::PacketType::KEEPALIVE;
                sendto(socket_fd, header, sizeof(*header), 0, 
                       (struct sockaddr*)&client_addr, client_len);
            }
        }
    }

    void stop() { running = false; }
};
}}