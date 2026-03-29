#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/latency.h>
#include "../extension/dsp_accel.h"
#include "../../../sdk/include/dsp_accel_sdk.h"
#include <string.h>
#include <iostream>

// Forward declarations
static bool plugin_init(const struct clap_plugin *plugin);
static void plugin_destroy(const struct clap_plugin *plugin);
static bool plugin_activate(const struct clap_plugin *plugin, double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count);
static void plugin_deactivate(const struct clap_plugin *plugin);
static bool plugin_start_processing(const struct clap_plugin *plugin);
static void plugin_stop_processing(const struct clap_plugin *plugin);
static void plugin_reset(const struct clap_plugin *plugin);
static clap_process_status plugin_process(const struct clap_plugin *plugin, const clap_process_t *process);
static const void *plugin_get_extension(const struct clap_plugin *plugin, const char *id);
static void plugin_on_main_thread(const struct clap_plugin *plugin);

// Plugin class structure

// Plugin instance data
struct MyPlugin {
    clap_plugin_t plugin;
    const clap_host_t *host;

    // Custom HW Accel Extension from Host
    const clap_host_dsp_accel_t *host_dsp_accel;

    // SDK context: manages Unix socket + IPC ring buffer
    dsp_accel_sdk_ctx_t *sdk_ctx;
};

static bool plugin_init(const struct clap_plugin *plugin) {
    auto *p = static_cast<MyPlugin *>(plugin->plugin_data);
    p->sdk_ctx = nullptr;
    p->host_dsp_accel = static_cast<const clap_host_dsp_accel_t *>(
        p->host->get_extension(p->host, CLAP_EXT_DSP_ACCEL)
    );
    return true;
}

static void plugin_destroy(const struct clap_plugin *plugin) {
    auto *p = static_cast<MyPlugin *>(plugin->plugin_data);
    // Fix #7: sdk_ctx may already be null if deactivate() ran first
    if (p->sdk_ctx) { dsp_accel_sdk_disconnect(p->sdk_ctx); p->sdk_ctx = nullptr; }
    delete p;
}

static clap_process_status plugin_process(const struct clap_plugin *plugin, const clap_process_t *process) {
    auto *p = static_cast<MyPlugin *>(plugin->plugin_data);
    bool processed_by_hw = false;

    // 1. Try hardware path
    if (p->sdk_ctx) {
        float* channel_ptrs[8] = {};
        uint32_t ch_count = 0;
        if (process->audio_inputs_count > 0) {
            const auto *in = &process->audio_inputs[0];
            ch_count = in->channel_count < 8 ? in->channel_count : 8;
            for (uint32_t ch = 0; ch < ch_count; ch++)
                channel_ptrs[ch] = in->data32[ch];
        }
        bool sent = dsp_accel_sdk_dispatch(
            p->sdk_ctx, channel_ptrs, (int)process->frames_count, (int)ch_count
        );
        if (sent && process->audio_outputs_count > 0) {
            // Fix #1: read GPU output into DAW output buffers
            float* out_ptrs[8] = {};
            auto *out = &process->audio_outputs[0];
            uint32_t out_ch = out->channel_count < 8 ? out->channel_count : 8;
            for (uint32_t ch = 0; ch < out_ch; ch++)
                out_ptrs[ch] = out->data32[ch];
            processed_by_hw = dsp_accel_sdk_read_output(
                p->sdk_ctx, out_ptrs, (int)process->frames_count, (int)out_ch
            );
        }
    }

    // 2. CPU FALLBACK — only if SDK dispatch failed or no daemon running
    if (!processed_by_hw) {
        for (uint32_t i = 0; i < process->audio_outputs_count; ++i) {
            if (i < process->audio_inputs_count) {
                const auto *in = &process->audio_inputs[i];
                auto *out = &process->audio_outputs[i];
                for (uint32_t ch = 0; ch < out->channel_count; ++ch) {
                    float *out_data = out->data32[ch];
                    const float *in_data = (ch < in->channel_count) ? in->data32[ch] : nullptr;
                    for (uint32_t f = 0; f < process->frames_count; ++f)
                        out_data[f] = in_data ? in_data[f] * 0.5f : 0.0f;
                }
            }
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

static clap_dsp_workload_type_t plugin_get_workload_type(const clap_plugin_t *plugin) {
    return DSP_TYPE_MASSIVELY_PARALLEL; // We want GPU acceleration!
}

static float plugin_get_estimated_cost(const clap_plugin_t *plugin) {
    return 150.0f; // Fake estimate: 150 MFLOPS per block
}

static const clap_plugin_dsp_accel_metadata_t s_metadata_ext = {
    .get_workload_type = plugin_get_workload_type,
    .get_estimated_cost = plugin_get_estimated_cost
};

static const void *plugin_get_extension(const struct clap_plugin *plugin, const char *id) {
    if (strcmp(id, CLAP_EXT_DSP_ACCEL_METADATA) == 0) {
        return &s_metadata_ext;
    }
    return nullptr;
}

// activate: open hardware SDK connection and report latency to host
static bool plugin_activate(const struct clap_plugin *plugin, double, uint32_t, uint32_t) {
    auto *p = static_cast<MyPlugin *>(plugin->plugin_data);
    p->sdk_ctx = dsp_accel_sdk_connect(DSP_ACCEL_TYPE_MASSIVELY_PARALLEL, 150.0f);
    // Fix #4: notify host of hardware latency for Plugin Delay Compensation
    if (p->sdk_ctx) {
        const auto *host_latency = static_cast<const clap_host_latency_t *>(
            p->host->get_extension(p->host, CLAP_EXT_LATENCY)
        );
        if (host_latency)
            host_latency->changed(p->host);
    }
    return true;
}
static void plugin_deactivate(const struct clap_plugin *plugin) {
    auto *p = static_cast<MyPlugin *>(plugin->plugin_data);
    if (p->sdk_ctx) { dsp_accel_sdk_disconnect(p->sdk_ctx); p->sdk_ctx = nullptr; }
}
static bool plugin_start_processing(const struct clap_plugin*) { return true; }
static void plugin_stop_processing(const struct clap_plugin*) {}
static void plugin_reset(const struct clap_plugin*) {}
static void plugin_on_main_thread(const struct clap_plugin*) {}

// Factory entry point
static uint32_t plugin_factory_get_plugin_count(const struct clap_plugin_factory *factory) { return 1; }
static const clap_plugin_descriptor_t *plugin_factory_get_plugin_descriptor(const struct clap_plugin_factory *factory, uint32_t index) {
    static const char *features[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr };
    static const clap_plugin_descriptor_t desc = {
        .clap_version = CLAP_VERSION_INIT,
        .id = "com.example.dsp_test_plugin",
        .name = "DSP Accel Dummy",
        .vendor = "Example",
        .url = "",
        .manual_url = "",
        .support_url = "",
        .version = "1.0.0",
        .description = "Hardware Accel Test Plugin",
        .features = features
    };
    return &desc;
}
static const clap_plugin_t *plugin_factory_create_plugin(const struct clap_plugin_factory *factory, const clap_host_t *host, const char *plugin_id) {
    if (strcmp(plugin_id, "com.example.dsp_test_plugin") != 0) return nullptr;
    auto *p = new MyPlugin();
    p->host = host;
    p->plugin.desc = plugin_factory_get_plugin_descriptor(factory, 0);
    p->plugin.plugin_data = p;
    p->plugin.init = plugin_init;
    p->plugin.destroy = plugin_destroy;
    p->plugin.activate = plugin_activate;
    p->plugin.deactivate = plugin_deactivate;
    p->plugin.start_processing = plugin_start_processing;
    p->plugin.stop_processing = plugin_stop_processing;
    p->plugin.reset = plugin_reset;
    p->plugin.process = plugin_process;
    p->plugin.get_extension = plugin_get_extension;
    p->plugin.on_main_thread = plugin_on_main_thread;
    return &p->plugin;
}

static const clap_plugin_factory_t plugin_factory = {
    .get_plugin_count = plugin_factory_get_plugin_count,
    .get_plugin_descriptor = plugin_factory_get_plugin_descriptor,
    .create_plugin = plugin_factory_create_plugin,
};

// Entry point required by DAW
extern "C" const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = [](const char *) { return true; },
    .deinit = []() {},
    .get_factory = [](const char *factory_id) -> const void * {
        if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) return &plugin_factory;
        return nullptr;
    }
};
