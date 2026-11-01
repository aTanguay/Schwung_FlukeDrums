#include <cmath>
#include <cstdio>
#include <cstdlib>
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

static int get_param(plugin_api_v2_t *api, void *inst, const char *key, char *buf, int buf_len) {
    std::memset(buf, 0, (size_t)buf_len);
    int n = api->get_param(inst, key, buf, buf_len);
    if (n < 0) {
        std::fprintf(stderr, "FAIL: get_param(%s) failed\n", key);
        return 1;
    }
    return 0;
}

static int expect_str(plugin_api_v2_t *api, void *inst, const char *key, const char *want) {
    char got[128];
    if (get_param(api, inst, key, got, (int)sizeof(got))) return 1;
    if (std::strcmp(got, want) != 0) {
        std::fprintf(stderr, "FAIL: %s expected '%s', got '%s'\n", key, want, got);
        return 1;
    }
    return 0;
}

static int expect_float(plugin_api_v2_t *api, void *inst, const char *key, float want) {
    char got[128];
    if (get_param(api, inst, key, got, (int)sizeof(got))) return 1;
    float gv = (float)std::atof(got);
    if (std::fabs(gv - want) > 0.0002f) {
        std::fprintf(stderr, "FAIL: %s expected %.4f, got %.4f\n", key, want, gv);
        return 1;
    }
    return 0;
}

int main() {
    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    if (!api || !api->create_instance || !api->set_param || !api->get_param || !api->destroy_instance) {
        std::fprintf(stderr, "FAIL: plugin api unavailable\n");
        return 2;
    }

    void *inst_a = api->create_instance(".", "{}");
    if (!inst_a) {
        std::fprintf(stderr, "FAIL: create_instance A failed\n");
        return 2;
    }

    api->set_param(inst_a, "g_master_vol", "0.73");
    api->set_param(inst_a, "g_polyphony", "12");
    api->set_param(inst_a, "ui_current_pad", "5");
    api->set_param(inst_a, "p01_pan", "0.5");
    api->set_param(inst_a, "p02_mode", "gate");
    api->set_param(inst_a, "p16_chance_pct", "55");

    char state[32768];
    if (get_param(api, inst_a, "state", state, (int)sizeof(state))) {
        api->destroy_instance(inst_a);
        return 1;
    }

    void *inst_b = api->create_instance(".", state);
    if (!inst_b) {
        std::fprintf(stderr, "FAIL: create_instance B failed\n");
        api->destroy_instance(inst_a);
        return 2;
    }

    int rc = 0;
    rc |= expect_float(api, inst_b, "g_master_vol", 0.73f);
    rc |= expect_str(api, inst_b, "g_polyphony", "12");
    rc |= expect_str(api, inst_b, "ui_current_pad", "5");
    rc |= expect_float(api, inst_b, "p01_pan", 0.5f);
    rc |= expect_str(api, inst_b, "p02_mode", "gate");
    rc |= expect_float(api, inst_b, "p16_chance_pct", 55.0f);

    api->destroy_instance(inst_b);
    api->destroy_instance(inst_a);

    if (rc) return 1;
    std::printf("PASS: mrdrums plugin state roundtrip\n");
    return 0;
}
