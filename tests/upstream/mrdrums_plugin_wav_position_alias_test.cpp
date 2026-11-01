#include <cstdio>
#include <cstring>
#include <cstdlib>

extern "C" {
typedef struct host_api_v1 {
    unsigned int api_version;
    int sample_rate;
    int frames_per_block;
    unsigned char *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char *msg);
    int (*midi_send_internal)(const unsigned char *msg, int len);
    int (*midi_send_external)(const unsigned char *msg, int len);
} host_api_v1_t;

typedef struct plugin_api_v2 {
    unsigned int api_version;
    void *(*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const unsigned char *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, short *out_interleaved_lr, int frames);
} plugin_api_v2_t;

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);
}

static int fail(const char *msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main() {
    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    if (!api || !api->create_instance || !api->set_param || !api->get_param || !api->destroy_instance) {
        return fail("plugin api unavailable");
    }

    void *inst = api->create_instance(".", "{}");
    if (!inst) return fail("create_instance failed");

    api->set_param(inst, "ui_current_pad", "1");
    api->set_param(inst, "p01_start", "0.0000");

    api->set_param(inst, "pad_start", "0.25");

    char pad_start_raw[64];
    std::memset(pad_start_raw, 0, sizeof(pad_start_raw));
    if (api->get_param(inst, "pad_start", pad_start_raw, (int)sizeof(pad_start_raw)) < 0) {
        api->destroy_instance(inst);
        return fail("get pad_start failed");
    }

    char p01_start_raw[64];
    std::memset(p01_start_raw, 0, sizeof(p01_start_raw));
    if (api->get_param(inst, "p01_start", p01_start_raw, (int)sizeof(p01_start_raw)) < 0) {
        api->destroy_instance(inst);
        return fail("get p01_start failed");
    }

    const float pad_start_val = (float)std::atof(pad_start_raw);
    const float p01_start_norm = (float)std::atof(p01_start_raw);
    if (pad_start_val < 0.249f || pad_start_val > 0.251f) {
        std::fprintf(stderr, "FAIL: pad_start expected ~0.25, got %s\n", pad_start_raw);
        api->destroy_instance(inst);
        return 1;
    }
    if (p01_start_norm < 0.249f || p01_start_norm > 0.251f) {
        std::fprintf(stderr, "FAIL: p01_start expected ~0.25, got %s\n", p01_start_raw);
        api->destroy_instance(inst);
        return 1;
    }

    char chain_params[65536];
    std::memset(chain_params, 0, sizeof(chain_params));
    if (api->get_param(inst, "chain_params", chain_params, (int)sizeof(chain_params)) < 0) {
        api->destroy_instance(inst);
        return fail("get chain_params failed");
    }

    if (!std::strstr(chain_params, "\"key\":\"pad_start\",\"name\":\"Start\",\"type\":\"wav_position\"")) {
        api->destroy_instance(inst);
        return fail("pad_start is not exposed as native wav_position type");
    }
    if (!std::strstr(chain_params, "\"key\":\"pad_start\",\"name\":\"Start\",\"type\":\"wav_position\",\"mode\":\"start\",\"filepath_param\":\"pad_sample_path\",\"min\":0,\"max\":1,\"step\":0.01")) {
        api->destroy_instance(inst);
        return fail("pad_start wav_position range should be normalized 0..1");
    }
    if (!std::strstr(chain_params, "\"filepath_param\":\"pad_sample_path\"")) {
        api->destroy_instance(inst);
        return fail("pad_start wav_position filepath linkage missing");
    }

    api->destroy_instance(inst);
    std::printf("PASS: mrdrums wav_position alias uses normalized 0..1 values\n");
    return 0;
}
