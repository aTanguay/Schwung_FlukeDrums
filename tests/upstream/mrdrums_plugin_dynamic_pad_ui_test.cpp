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

static int expect_ui_pad_key(plugin_api_v2_t *api, void *inst, const char *want_key_fragment) {
    char hier[4096];
    std::memset(hier, 0, sizeof(hier));
    if (api->get_param(inst, "ui_hierarchy", hier, (int)sizeof(hier)) < 0) {
        return fail("get_param(ui_hierarchy) failed");
    }
    if (!std::strstr(hier, want_key_fragment)) {
        std::fprintf(stderr, "FAIL: expected ui_hierarchy to contain %s\n", want_key_fragment);
        return 1;
    }
    return 0;
}

static int expect_ui_hierarchy_contains(plugin_api_v2_t *api, void *inst, const char *needle) {
    char hier[4096];
    std::memset(hier, 0, sizeof(hier));
    if (api->get_param(inst, "ui_hierarchy", hier, (int)sizeof(hier)) < 0) {
        return fail("get_param(ui_hierarchy) failed");
    }
    if (!std::strstr(hier, needle)) {
        std::fprintf(stderr, "FAIL: expected ui_hierarchy to contain %s\n", needle);
        return 1;
    }
    return 0;
}

int main() {
    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    if (!api || !api->create_instance || !api->set_param || !api->get_param || !api->on_midi || !api->destroy_instance) {
        return fail("plugin api unavailable");
    }

    void *inst = api->create_instance(".", "{}");
    if (!inst) return fail("create_instance failed");

    api->set_param(inst, "ui_current_pad", "6");
    if (expect_ui_pad_key(api, inst, "pad_pan") != 0) {
        api->destroy_instance(inst);
        return 1;
    }
    if (expect_ui_hierarchy_contains(api, inst, "\"global\":{\"name\":\"Global\",\"params\":[\"g_master_vol\",\"g_polyphony\",\"g_vel_curve\",\"g_humanize_ms\",\"g_rand_seed\",\"g_rand_loop_steps\"],\"knobs\":[\"g_master_vol\",\"g_polyphony\",\"g_vel_curve\",\"g_humanize_ms\",\"g_rand_seed\",\"g_rand_loop_steps\"]}") != 0) {
        api->destroy_instance(inst);
        return 1;
    }
    if (expect_ui_hierarchy_contains(api, inst, "\"root\":{\"name\":\"MrDrums\",\"params\":[{\"label\":\"Global\",\"level\":\"global\"},{\"label\":\"Pad Settings\",\"level\":\"pad_settings\"},\"ui_preset_path\"],\"knobs\":[\"ui_auto_select_pad\",\"pad_vol\",\"pad_pan\",\"pad_tune\",\"pad_start\",\"pad_attack_ms\",\"pad_decay_ms\",\"pad_choke_group\",\"pad_mode\"]}") != 0) {
        api->destroy_instance(inst);
        return 1;
    }
    if (expect_ui_hierarchy_contains(api, inst, "\"params\":[\"ui_auto_select_pad\",\"ui_current_pad\",\"pad_sample_path\",\"pad_vol\",\"pad_pan\",\"pad_tune\",\"pad_start\",\"pad_attack_ms\",\"pad_decay_ms\",\"pad_choke_group\",\"pad_mode\",\"pad_rand_pan_amt\",\"pad_rand_vol_amt\",\"pad_rand_decay_amt\",\"pad_chance_pct\"]") != 0) {
        api->destroy_instance(inst);
        return 1;
    }
    if (expect_ui_hierarchy_contains(api, inst, "\"knobs\":[\"ui_auto_select_pad\",\"pad_vol\",\"pad_pan\",\"pad_tune\",\"pad_start\",\"pad_attack_ms\",\"pad_decay_ms\",\"pad_choke_group\",\"pad_mode\"]") != 0) {
        api->destroy_instance(inst);
        return 1;
    }

    const unsigned char note_on_pad9[3] = {0x90, 44, 100};
    api->on_midi(inst, note_on_pad9, 3, 0);

    char cur_pad[32];
    std::memset(cur_pad, 0, sizeof(cur_pad));
    if (api->get_param(inst, "ui_current_pad", cur_pad, (int)sizeof(cur_pad)) < 0) {
        api->destroy_instance(inst);
        return fail("get ui_current_pad failed");
    }
    if (std::strcmp(cur_pad, "9") != 0) {
        std::fprintf(stderr, "FAIL: ui_current_pad expected 9 after note-on, got %s\n", cur_pad);
        api->destroy_instance(inst);
        return 1;
    }

    if (expect_ui_pad_key(api, inst, "pad_pan") != 0) {
        api->destroy_instance(inst);
        return 1;
    }

    api->destroy_instance(inst);
    std::printf("PASS: mrdrums dynamic pad ui mapping\n");
    return 0;
}
