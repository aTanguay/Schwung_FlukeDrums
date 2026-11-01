#include <cstdio>
#include <cstring>

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
    if (!api || !api->create_instance || !api->get_param || !api->destroy_instance) {
        return fail("plugin api unavailable");
    }

    void *inst = api->create_instance(".", "{}");
    if (!inst) return fail("create_instance failed");

    char chain_params[65536];
    std::memset(chain_params, 0, sizeof(chain_params));
    if (api->get_param(inst, "chain_params", chain_params, (int)sizeof(chain_params)) < 0) {
        api->destroy_instance(inst);
        return fail("get chain_params failed");
    }

    if (!std::strstr(chain_params, "\"key\":\"pad_sample_path\"")) {
        api->destroy_instance(inst);
        return fail("pad_sample_path metadata missing");
    }
    if (std::strstr(chain_params, "\"name\":\"Current Sample\"")) {
        api->destroy_instance(inst);
        return fail("pad_sample_path label should not use Current prefix");
    }
    if (!std::strstr(chain_params, "\"root\":\"/data\"")) {
        api->destroy_instance(inst);
        return fail("filepath root not set to /data");
    }
    if (!std::strstr(chain_params, "\"start_path\":\"/data/UserData/UserLibrary/Samples\"")) {
        api->destroy_instance(inst);
        return fail("filepath start_path metadata missing");
    }
    if (!std::strstr(chain_params, "\"filter\":[\".wav\",\".aif\",\".aiff\"]")) {
        api->destroy_instance(inst);
        return fail("filepath filter missing wav/aif/aiff support");
    }
    if (!std::strstr(chain_params, "\"live_preview\":true")) {
        api->destroy_instance(inst);
        return fail("filepath live_preview metadata missing");
    }
    if (!std::strstr(chain_params, "\"browser_hooks\":{\"on_open\":[{\"key\":\"ui_auto_select_pad\",\"value\":\"off\",\"restore\":true}]}")) {
        api->destroy_instance(inst);
        return fail("filepath browser_hooks metadata missing");
    }
    if (!std::strstr(chain_params, "\"key\":\"ui_preset_path\"")) {
        api->destroy_instance(inst);
        return fail("ui_preset_path metadata missing");
    }
    if (!std::strstr(chain_params, "\"key\":\"ui_preset_path\",\"name\":\"Load Preset\",\"type\":\"filepath\",\"root\":\"/data\",\"start_path\":\"/data/UserData/UserLibrary/Track Presets\",\"filter\":[\".ablpreset\",\".json\"]")) {
        api->destroy_instance(inst);
        return fail("ui_preset_path metadata missing /data root, expected start path, or json filter");
    }

    api->set_param(inst, "ui_last_sample_dir", "/data/UserData/UserLibrary/Samples/Drums/Kicks/Kick07.wav");
    std::memset(chain_params, 0, sizeof(chain_params));
    if (api->get_param(inst, "chain_params", chain_params, (int)sizeof(chain_params)) < 0) {
        api->destroy_instance(inst);
        return fail("get chain_params after ui_last_sample_dir failed");
    }

    if (!std::strstr(chain_params, "\"start_path\":\"/data/UserData/UserLibrary/Samples/Drums/Kicks/Kick07.wav\"")) {
        api->destroy_instance(inst);
        return fail("filepath start_path did not follow ui_last_sample_dir");
    }
    if (!std::strstr(chain_params, "\"live_preview\":true")) {
        api->destroy_instance(inst);
        return fail("filepath live_preview metadata missing after ui_last_sample_dir");
    }
    if (!std::strstr(chain_params, "\"browser_hooks\":{\"on_open\":[{\"key\":\"ui_auto_select_pad\",\"value\":\"off\",\"restore\":true}]}")) {
        api->destroy_instance(inst);
        return fail("filepath browser_hooks metadata missing after ui_last_sample_dir");
    }

    api->destroy_instance(inst);
    std::printf("PASS: mrdrums filepath start_path metadata (default + dynamic)\n");
    return 0;
}
