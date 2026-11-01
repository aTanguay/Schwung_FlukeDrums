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

static int fail(const char *msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static int expect_int(plugin_api_v2_t *api, void *inst, const char *key, int expected) {
    char buf[64];
    std::memset(buf, 0, sizeof(buf));
    if (api->get_param(inst, key, buf, (int)sizeof(buf)) < 0) {
        std::fprintf(stderr, "FAIL: get_param(%s) failed\n", key);
        return 1;
    }
    int got = std::atoi(buf);
    if (got != expected) {
        std::fprintf(stderr, "FAIL: %s expected %d got %d\n", key, expected, got);
        return 1;
    }
    return 0;
}

static int expect_str(plugin_api_v2_t *api, void *inst, const char *key, const char *expected) {
    char buf[64];
    std::memset(buf, 0, sizeof(buf));
    if (api->get_param(inst, key, buf, (int)sizeof(buf)) < 0) {
        std::fprintf(stderr, "FAIL: get_param(%s) failed\n", key);
        return 1;
    }
    if (std::strcmp(buf, expected) != 0) {
        std::fprintf(stderr, "FAIL: %s expected %s got %s\n", key, expected, buf);
        return 1;
    }
    return 0;
}

int main() {
    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    if (!api || !api->create_instance || !api->destroy_instance || !api->set_param || !api->get_param || !api->on_midi) {
        return fail("plugin api unavailable");
    }

    void *inst = api->create_instance(".", "{}");
    if (!inst) return fail("create_instance failed");

    api->set_param(inst, "ui_current_pad", "1");
    api->set_param(inst, "ui_auto_select_pad", "0");
    if (expect_str(api, inst, "ui_auto_select_pad", "off") != 0) {
        api->destroy_instance(inst);
        return 1;
    }

    const unsigned char note_on_pad5[3] = {0x90, 40, 110};  // note 40 => pad 5
    api->on_midi(inst, note_on_pad5, 3, 0);
    if (expect_int(api, inst, "ui_current_pad", 1) != 0) {
        api->destroy_instance(inst);
        return 1;
    }

    api->set_param(inst, "ui_auto_select_pad", "1");
    if (expect_str(api, inst, "ui_auto_select_pad", "on") != 0) {
        api->destroy_instance(inst);
        return 1;
    }
    const unsigned char note_on_pad7[3] = {0x90, 42, 110};  // note 42 => pad 7
    api->on_midi(inst, note_on_pad7, 3, 0);
    if (expect_int(api, inst, "ui_current_pad", 7) != 0) {
        api->destroy_instance(inst);
        return 1;
    }

    api->destroy_instance(inst);
    std::printf("PASS: mrdrums ui auto-select pad toggle\n");
    return 0;
}
