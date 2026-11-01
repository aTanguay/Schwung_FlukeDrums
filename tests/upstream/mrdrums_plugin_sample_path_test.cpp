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

int main() {
    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    if (!api || !api->create_instance || !api->set_param || !api->get_param || !api->destroy_instance) {
        std::fprintf(stderr, "FAIL: plugin api unavailable\n");
        return 2;
    }

    void *inst = api->create_instance(".", "{}");
    if (!inst) {
        std::fprintf(stderr, "FAIL: create_instance failed\n");
        return 2;
    }

    api->set_param(inst, "p01_sample_path", "/tmp/not-found.wav");

    char got[512];
    std::memset(got, 0, sizeof(got));
    if (api->get_param(inst, "p01_sample_path", got, (int)sizeof(got)) < 0) {
        std::fprintf(stderr, "FAIL: get p01_sample_path failed\n");
        api->destroy_instance(inst);
        return 1;
    }

    if (std::strcmp(got, "/tmp/not-found.wav") != 0) {
        std::fprintf(stderr, "FAIL: expected stored missing path, got '%s'\n", got);
        api->destroy_instance(inst);
        return 1;
    }

    api->set_param(inst, "p01_sample_path", "");
    std::memset(got, 0, sizeof(got));
    if (api->get_param(inst, "p01_sample_path", got, (int)sizeof(got)) < 0) {
        std::fprintf(stderr, "FAIL: get p01_sample_path after clear failed\n");
        api->destroy_instance(inst);
        return 1;
    }

    if (std::strcmp(got, "") != 0) {
        std::fprintf(stderr, "FAIL: expected empty path after clear, got '%s'\n", got);
        api->destroy_instance(inst);
        return 1;
    }

    api->destroy_instance(inst);
    std::printf("PASS: mrdrums plugin sample path persistence\n");
    return 0;
}
