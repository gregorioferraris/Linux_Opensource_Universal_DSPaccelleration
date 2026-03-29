#include <pipewire/pipewire.h>
#include <pipewire/impl.h>
#include <pipewire/core.h>
#include <spa/utils/defs.h>
#include <spa/utils/list.h>
#include <spa/utils/hook.h>
#include <spa/support/loop.h>
#include <sys/eventfd.h>
#include <atomic>
#include <errno.h>
#include <string.h>
#include "../ipc/include/ipc_shm.hpp"
#include "../../sdk/include/dsp_accel_sdk.h"

extern "C" {

#define PW_LOG_MODULE_NAME "dsp-accel"
#define NAME "module-dsp-accel"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "../../clap/extension/dsp_accel.h"

#define HEARTBEAT_TIMEOUT_MS 500

struct dsp_accel_node {
    DspAccel::Ipc::DspSharedMemory *shm;
    int worker_id;
    uint64_t last_heartbeat;
    bool is_active;
    pid_t worker_pid;
    int shm_fd;      
    int e_send;      
    int e_recv;      
};

#define DSP_DAEMON_SOCKET_PATH "/tmp/dsp_accel_daemon.sock"

struct module_dsp_accel_data {
    struct pw_context *context;
    struct pw_impl_module *module;
    struct spa_hook module_listener;
    
    struct pw_loop *main_loop;
    struct spa_source *socket_source;
    int server_fd;

    // Hardware Pool
    int num_compute_nodes;
    struct dsp_accel_node *compute_nodes;
};

// Helper: send FD via SCM_RIGHTS
static int pass_fd_to_child(int socket, int fd) {
    char buf[1] = { 0 };
    struct iovec iov = { .iov_base = buf, .iov_len = 1 };
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {
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

static void spawn_worker(struct module_dsp_accel_data *d, int node_index) {
    struct dsp_accel_node *node = &d->compute_nodes[node_index];
    
    // 1. Create memfd and eventfds (Blocking for worker RT loop)
    node->shm_fd = memfd_create("dsp_accel_shm", MFD_CLOEXEC);
    if (node->shm_fd < 0) return;
    if (ftruncate(node->shm_fd, sizeof(DspAccel::Ipc::DspSharedMemory)) < 0) return;
    
    node->e_send = eventfd(0, EFD_CLOEXEC); 
    node->e_recv = eventfd(0, EFD_CLOEXEC); 
    
    // 2. Map SHM locally for the Supervisor
    node->shm = (DspAccel::Ipc::DspSharedMemory*)mmap(NULL, sizeof(DspAccel::Ipc::DspSharedMemory), 
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, node->shm_fd, 0);
    if (node->shm == MAP_FAILED) {
        pw_log_error("Supervisor: Failed to mmap SHM for worker %d", node->worker_id);
        node->shm = nullptr;
        return;
    }
    
    // 3. Create control socket for FD passing
    int sv[2]; 
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) return;

    pw_log_info("Supervisor: Spawning Worker %d (SHM FD: %d)", node->worker_id, node->shm_fd);
    
    node->worker_pid = fork();
    if (node->worker_pid == 0) {
        // --- CHILD (Worker) ---
        close(sv[0]); 
        dup2(sv[1], STDIN_FILENO);
        close(sv[1]);
        close(node->shm_fd); 
        close(node->e_send);
        close(node->e_recv);

        const char* type_str = (node->worker_id == 0) ? "vulkan" : 
                               (node->worker_id == 1) ? "npu" : "dsp";
        char id_str[16];
        snprintf(id_str, sizeof(id_str), "%i", node->worker_id);

        // Use an environment variable for the worker path, or fallback to a sensible default
        const char* worker_path = getenv("DSP_WORKER_BIN");
        if (!worker_path) worker_path = "./daemon/pipewire/dsp-accel-worker";

        pw_log_info("Supervisor: Attempting to execute worker at %s", worker_path);
        execl(worker_path, "dsp-accel-worker", "--type", type_str, "--id", id_str, NULL);
        
        pw_log_error("Supervisor: execl failed: %s", strerror(errno));
        _exit(1); 
    } else if (node->worker_pid > 0) {
        // --- PARENT (Supervisor) ---
        close(sv[1]);
        
        // Pass all 3 FDs to the worker
        pass_fd_to_child(sv[0], node->shm_fd);
        pass_fd_to_child(sv[0], node->e_send);
        pass_fd_to_child(sv[0], node->e_recv);
        close(sv[0]);
        
        node->is_active = true;
    } else {
        pw_log_error("Supervisor: Fork failed for worker %d", node->worker_id);
    }
}

static int handle_plugin_request(struct module_dsp_accel_data *d, const clap_plugin_dsp_accel_metadata_t *metadata, const clap_plugin_t *plugin) {
    if (!metadata || !d || d->num_compute_nodes == 0) return -1;
    
    // 1. Identify optimal backend for this workload
    DspWorkloadType type = (DspWorkloadType)metadata->get_workload_type(plugin);
    
    // 2. Supervisor Routing: Find a healthy worker for this type
    struct dsp_accel_node *best_node = nullptr;
    for (int i = 0; i < d->num_compute_nodes; i++) {
        struct dsp_accel_node *node = &d->compute_nodes[i];
        if (!node->shm) continue;

        // 2. Supervisor Routing: Dynamic Load Balancing
        auto& ctrl = node->shm->workers[node->worker_id];
        uint32_t load = ctrl.load_pct.load(std::memory_order_relaxed);
        
        if (ctrl.last_error.load(std::memory_order_relaxed) != DspAccel::Ipc::WorkerError::NONE) {
            pw_log_error("Supervisor: Worker %d reported error! Restarting.", node->worker_id);
            node->is_active = false;
            spawn_worker(d, i); 
            continue;
        }

        if (load >= 80) {
            pw_log_warn("Supervisor: Worker %d OVERLOADED (%u%%). CPU Fallback.", node->worker_id, load);
            continue;
        }

        uint64_t hb = ctrl.heartbeat.load(std::memory_order_acquire);
        if (hb == node->last_heartbeat) {
             pw_log_warn("Supervisor: Worker %d heartbeat stalled.", node->worker_id);
             // In a production system, we'd wait a few cycles before declaring dead
        }
        node->last_heartbeat = hb;

        if (node->is_active) {
            best_node = node;
            break;
        }
    }

    if (!best_node) {
        pw_log_warn("Supervisor: No healthy workers available. CPU Fallback.");
        return -1;
    }

    // In the CLAP extension context, we return 0 to indicate success.
    // The actual FD passing is handled via the Unix socket in on_plugin_connection.
    pw_log_info("Supervisor: Routing plugin request to worker %d", best_node->worker_id);
    return 0;
}

static void on_plugin_connection(void *data, int fd, uint32_t mask) {
    auto *d = (struct module_dsp_accel_data *)data;
    if (mask & SPA_IO_IN) {
        int client_fd = accept(fd, NULL, NULL);
        if (client_fd < 0) return;

        pw_log_info("Supervisor: New plugin session request.");

        // 1. Receive metadata from plugin
        struct { uint32_t wl_type; float cost; } meta;
        if (recv(client_fd, &meta, sizeof(meta), 0) != sizeof(meta)) {
            close(client_fd);
            return;
        }

        // 2. Select worker
        struct dsp_accel_node *target = NULL;
        for (int i = 0; i < d->num_compute_nodes; i++) {
            // Simplified: match type (0=Vulkan, 1=NPU, 2=DSP)
            if (i == (int)meta.wl_type && d->compute_nodes[i].is_active) {
                target = &d->compute_nodes[i];
                break;
            }
        }

        if (!target) {
            pw_log_warn("Supervisor: No suitable worker for type %d", meta.wl_type);
            close(client_fd);
            return;
        }

        // 3. Send Handshake Response: {worker_id}
        struct { uint32_t worker_id; } response = { (uint32_t)target->worker_id };
        if (send(client_fd, &response, sizeof(response), 0) != sizeof(response)) {
            close(client_fd);
            return;
        }

        // 4. Send FDs to plugin: SHM, e_send, e_recv
        pass_fd_to_child(client_fd, target->shm_fd);
        pass_fd_to_child(client_fd, target->e_send);
        pass_fd_to_child(client_fd, target->e_recv);
        
        pw_log_info("Supervisor: Session established for worker %d", target->worker_id);
        close(client_fd);
    }
}

static void module_destroy(void *data) {
    struct module_dsp_accel_data *d = (struct module_dsp_accel_data *)data;
    spa_hook_remove(&d->module_listener);
    
    if (d->socket_source) pw_loop_remove_source(d->main_loop, d->socket_source);
    if (d->server_fd >= 0) close(d->server_fd);
    unlink(DSP_DAEMON_SOCKET_PATH);

    if (d->compute_nodes) {
        for (int i = 0; i < d->num_compute_nodes; i++) {
            struct dsp_accel_node *node = &d->compute_nodes[i];
            if (node->shm && node->shm != MAP_FAILED) {
                munmap(node->shm, sizeof(DspAccel::Ipc::DspSharedMemory));
            }
            // Fix: cleanup FDs
            if (node->shm_fd >= 0) close(node->shm_fd);
            if (node->e_send >= 0) close(node->e_send);
            if (node->e_recv >= 0) close(node->e_recv);
        }
        free(d->compute_nodes);
    }
    
    pw_log_info("module " NAME ": destroyed.");
    free(d);
}

static const struct pw_impl_module_events module_events = {
    .version = PW_VERSION_IMPL_MODULE_EVENTS,
    .destroy = (void (*)(void *))module_destroy,
};

SPA_EXPORT
int pipewire__module_init(struct pw_impl_module *module, const char *args) {
    struct pw_context *context = pw_impl_module_get_context(module);
    struct module_dsp_accel_data *data;

    data = (struct module_dsp_accel_data *)calloc(1, sizeof(struct module_dsp_accel_data));
    if (data == NULL) return -ENOMEM;

    data->context = context;
    data->module = module;
    data->main_loop = pw_context_get_main_loop(context);

    // 1. Setup Plugin Listener Socket
    data->server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (data->server_fd >= 0) {
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, DSP_DAEMON_SOCKET_PATH, sizeof(addr.sun_path)-1);
        unlink(addr.sun_path);
        if (bind(data->server_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            listen(data->server_fd, 5);
            data->socket_source = pw_loop_add_io(data->main_loop, data->server_fd, 
                                                SPA_IO_IN, true, on_plugin_connection, data);
        } else {
            pw_log_error("Supervisor: Failed to bind socket: %s", strerror(errno));
            close(data->server_fd);
            data->server_fd = -1;
            return -errno; // Return actual error to PipeWire loader
        }
    } else {
        pw_log_error("Supervisor: Failed to create socket: %s", strerror(errno));
    }

    pw_log_info("module " NAME ": Supervisor Initialized. Listening on %s", DSP_DAEMON_SOCKET_PATH);

    pw_impl_module_add_listener(module, &data->module_listener, &module_events, data);

    // 2. Initialize Workers
    data->num_compute_nodes = 3; // Vulkan, NPU, DSP
    data->compute_nodes = (struct dsp_accel_node*)calloc(data->num_compute_nodes, sizeof(struct dsp_accel_node));
    
    for (int i = 0; i < data->num_compute_nodes; i++) {
        data->compute_nodes[i].worker_id = i;
        spawn_worker(data, i); // This now handles the real mmap
    }

    // 3. Setup MCP Monitoring Timer (Dump state to JSON every 500ms)
    auto dump_state = [](void *data, uint64_t expirations) {
        auto *d = (struct module_dsp_accel_data *)data;
        FILE *f = fopen("/tmp/dsp_accel_monitor.json", "w");
        if (!f) return;
        
        fprintf(f, "{\n  \"timestamp\": %lu,\n  \"workers\": [\n", time(NULL));
        for (int i = 0; i < d->num_compute_nodes; i++) {
            auto *node = &d->compute_nodes[i];
            if (!node->shm) continue; // Prevent Segfault if worker failed to init

            auto& ctrl = node->shm->workers[node->worker_id];
            fprintf(f, "    {\n      \"id\": %d,\n      \"type\": \"%s\",\n      \"status\": \"%s\",\n      \"load\": %u,\n      \"heartbeat\": %lu\n    }%s\n",
                    node->worker_id, 
                    (i == 0 ? "vulkan" : i == 1 ? "npu" : "dsp"),
                    (node->is_active ? "Healthy" : "Dead"),
                    ctrl.load_pct.load(std::memory_order_relaxed),
                    ctrl.heartbeat.load(std::memory_order_relaxed),
                    (i == d->num_compute_nodes - 1 ? "" : ","));
        }
        fprintf(f, "  ]\n}\n");
        fclose(f);
    };

    struct spa_source *timer = pw_loop_add_timer(data->main_loop, dump_state, data);
    
    struct timespec value, interval;
    value.tv_sec = 0; value.tv_nsec = 500 * 1000 * 1000;
    interval.tv_sec = 0; interval.tv_nsec = 500 * 1000 * 1000;
    pw_loop_update_timer(data->main_loop, timer, &value, &interval, false);

    return 0;
}

} // extern "C"
