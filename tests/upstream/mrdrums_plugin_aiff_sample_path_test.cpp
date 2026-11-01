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

static void write_test_aiff(const char *path) {
    static const unsigned char kAiff[] = {
        'F','O','R','M', 0x00,0x00,0x00,0x32, 'A','I','F','F',
        'C','O','M','M', 0x00,0x00,0x00,0x12, 0x00,0x01, 0x00,0x00,0x00,0x02,
        0x00,0x10, 0x40,0x0E, 0xAC,0x44, 0x00,0x00, 0x00,0x00, 0x00,0x00,
        'S','S','N','D', 0x00,0x00,0x00,0x0C, 0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00, 0x7F,0xFF, 0x00,0x00
    };

    FILE *f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "FAIL: could not write AIFF fixture\n");
        std::exit(2);
    }
    if (std::fwrite(kAiff, 1, sizeof(kAiff), f) != sizeof(kAiff)) {
        std::fprintf(stderr, "FAIL: incomplete AIFF fixture write\n");
        std::fclose(f);
        std::exit(2);
    }
    std::fclose(f);
}

int main() {
    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    if (!api || !api->create_instance || !api->set_param || !api->get_error || !api->render_block || !api->on_midi || !api->destroy_instance) {
        return fail("plugin api unavailable");
    }

    const char *aiff_path = "/tmp/mrdrums-test.aif";
    write_test_aiff(aiff_path);

    void *inst = api->create_instance(".", "{}");
    if (!inst) return fail("create_instance failed");

    api->set_param(inst, "p01_sample_path", aiff_path);

    char err[256];
    std::memset(err, 0, sizeof(err));
    if (api->get_error(inst, err, (int)sizeof(err)) > 0) {
        std::fprintf(stderr, "FAIL: unexpected plugin error after AIFF load: %s\n", err);
        api->destroy_instance(inst);
        return 1;
    }

    const unsigned char note_on[3] = {0x90, 36, 127};
    api->on_midi(inst, note_on, 3, 0);

    short out[128 * 2];
    std::memset(out, 0, sizeof(out));
    api->render_block(inst, out, 128);

    long sum = 0;
    for (size_t i = 0; i < sizeof(out) / sizeof(out[0]); i++) {
        int v = out[i];
        sum += (v < 0) ? -v : v;
    }
    if (sum <= 0) {
        api->destroy_instance(inst);
        return fail("AIFF sample produced silent output");
    }

    api->destroy_instance(inst);
    std::printf("PASS: mrdrums loads and plays AIFF sample paths\n");
    return 0;
}
