#include <iostream>
#include <vector>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <csignal>
#include <cstring>
#include <sys/wait.h> // For waitpid
#include "ipc/include/ipc_shm.hpp"

#define SOCKET_PATH "/tmp/dsp_accel_daemon.sock"

using namespace DspAccel::Ipc;

static bool running = true;
void handle_sig(int) { running = false; }

// Helper per inviare File Descriptor via SCM_RIGHTS
static int send_fd(int socket, int fd) {
    // This function is for sending FDs over an *already connected* Unix domain socket.
    // The current daemon/worker handshake is simplified.
    // In a real scenario, the supervisor would fork, then in the child, set up a socketpair,
    // send the FDs over one end of the socketpair, and then exec the worker.
    // For now, this is a placeholder.
    // The current implementation in dsp_accel_sdk.cpp and worker_main.cpp expects
    // the FDs to be received via recv_fd on the client_fd (SDK) or STDIN_FILENO (worker).
    // This `send_fd` is not directly used in the current daemon/worker fork/exec flow.
    // It's more relevant for the SDK-Daemon communication.

    // For the daemon to worker communication, the FDs are typically passed via `exec`
    // by setting up the file descriptors before the `exec` call, or by passing them
    // as arguments if the worker is designed to receive them that way.
    // The current worker_main.cpp expects them from STDIN_FILENO, which implies
    // the supervisor would redirect the FDs to the worker's stdin.
    // This is a complex IPC topic. For now, we'll keep the `send_fd` as is,
    // but acknowledge its current limited use in the daemon-worker fork/exec.

    // The `send_fd` in `dsp_accel_sdk.cpp` is correct for SDK-Daemon communication.
    // The `recv_fd` in `dsp_accel_sdk.cpp` and `daemon/worker_main.cpp` is also correct
    // for receiving FDs. The challenge is how the daemon *sends* them to the worker.
    // For the `fork/exec` scenario, it's usually done by duplicating FDs to stdin/stdout
    // or using a socketpair.
    char buf[1] = {0};
    struct iovec iov = { .iov_base = buf, .iov_len = 1 };
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {
        .msg_name = NULL, .msg_namelen = 0,
        .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = cmsg_buf, .msg_controllen = sizeof(cmsg_buf)
    };
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
    return sendmsg(socket, &msg, 0);
}

// Helper to receive FDs (used by worker, but also useful for daemon to receive from SDK if needed)
static int recv_fd(int socket) {
    // This is a placeholder. The actual implementation is in dsp_accel_sdk.cpp and worker_main.cpp
    // For daemon, it's more about sending FDs to the worker.
    return -1; 
}

int main() {
    std::signal(SIGINT, handle_sig);
    std::signal(SIGTERM, handle_sig);

    unlink(SOCKET_PATH);
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return 1;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    listen(server_fd, 5);
    std::cout << "[Daemon] DSPA Supervisor attivo su " << SOCKET_PATH << std::endl;

    while (running) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        std::cout << "[Daemon] Nuova connessione SDK rilevata." << std::endl;

        // 1. Ricezione metadati dal plugin
        struct { uint32_t wl_type; float cost; } meta;
        if (recv(client_fd, &meta, sizeof(meta), 0) <= 0) {
            close(client_fd);
            continue;
        }

        // 2. Creazione SHM (memfd)
        int shm_fd = memfd_create("dspa_shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (shm_fd < 0) {
            close(client_fd);
            continue;
        }
        ftruncate(shm_fd, sizeof(DspSharedMemory));

        // v1.0 Security: Applichiamo i sigilli (Sealing)
        // Impedisce ai plugin di ridimensionare la memoria e causare crash
        fcntl(shm_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL);

        // 3. Creazione canali di sincronizzazione (EventFD)
        int e_send = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        int e_recv = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

        // 4. Inizializzazione della memoria condivisa
        void* ptr = mmap(NULL, sizeof(DspSharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (ptr != MAP_FAILED) {
            memset(ptr, 0, sizeof(DspSharedMemory));
            // Qui potresti inizializzare i flag is_ready dei worker
            munmap(ptr, sizeof(DspSharedMemory));
        }

        // 5. Handshake: Invia Worker ID (fisso a 0 per ora)
        struct { uint32_t worker_id; } resp = { 0 };
        send(client_fd, &resp, sizeof(resp), 0);

        // 6. Invio dei 3 descrittori vitali (SHM, Event Send, Event Recv)
        send_fd(client_fd, shm_fd);
        send_fd(client_fd, e_send);
        send_fd(client_fd, e_recv);

        // Nota: In un sistema reale, qui dovresti fare il fork/exec del worker
        // associato a questo worker_id.

        close(client_fd);
        std::cout << "[Daemon] Handshake completato per Worker #0." << std::endl;
        
        // Per i test, teniamo i FD aperti o gestiamoli in un pool
        // close(shm_fd); close(e_send); close(e_recv); 
    }

    unlink(SOCKET_PATH);
    return 0;
}