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

static int expect_str(plugin_api_v2_t *api, void *inst, const char *key, const char *want) {
    char got[1024];
    std::memset(got, 0, sizeof(got));
    if (api->get_param(inst, key, got, (int)sizeof(got)) < 0) {
        std::fprintf(stderr, "FAIL: get_param(%s) failed\n", key);
        return 1;
    }
    if (std::strcmp(got, want) != 0) {
        std::fprintf(stderr, "FAIL: %s expected '%s' got '%s'\n", key, want, got);
        return 1;
    }
    return 0;
}

int main() {
    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    if (!api || !api->create_instance || !api->set_param || !api->get_param || !api->destroy_instance) {
        return fail("plugin api unavailable");
    }

    void *inst_a = api->create_instance(".", "{}");
    if (!inst_a) return fail("create_instance A failed");

    api->set_param(inst_a, "ui_last_sample_dir", "/data/UserData/UserLibrary/Samples/Drums/Kicks");
    if (expect_str(api, inst_a, "ui_last_sample_dir", "/data/UserData/UserLibrary/Samples/Drums/Kicks") != 0) {
        api->destroy_instance(inst_a);
        return 1;
    }

    char state[32768];
    std::memset(state, 0, sizeof(state));
    if (api->get_param(inst_a, "state", state, (int)sizeof(state)) < 0) {
        api->destroy_instance(inst_a);
        return fail("get state failed");
    }

    if (!std::strstr(state, "\"ui_last_sample_dir\":\"/data/UserData/UserLibrary/Samples/Drums/Kicks\"")) {
        api->destroy_instance(inst_a);
        return fail("state missing ui_last_sample_dir");
    }

    void *inst_b = api->create_instance(".", state);
    if (!inst_b) {
        api->destroy_instance(inst_a);
        return fail("create_instance B failed");
    }

    int rc = expect_str(api, inst_b, "ui_last_sample_dir", "/data/UserData/UserLibrary/Samples/Drums/Kicks");

    api->destroy_instance(inst_b);
    api->destroy_instance(inst_a);

    if (rc) return 1;
    std::printf("PASS: mrdrums last sample dir state roundtrip\n");
    return 0;
}
