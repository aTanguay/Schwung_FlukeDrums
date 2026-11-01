#include <cstdio>
#include <cstring>

#include "mrdrums_params.h"

static int fail(const char *msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main() {
    char key[32];
    if (!mrdrums_make_pad_key(1, "pan", key, sizeof(key))) return fail("pad1 key generation");
    if (std::strcmp(key, "p01_pan") != 0) return fail("expected p01_pan");

    if (!mrdrums_make_pad_key(16, "chance_pct", key, sizeof(key))) return fail("pad16 key generation");
    if (std::strcmp(key, "p16_chance_pct") != 0) return fail("expected p16_chance_pct");

    if (mrdrums_make_pad_key(0, "pan", key, sizeof(key))) return fail("pad 0 should be invalid");
    if (mrdrums_make_pad_key(17, "pan", key, sizeof(key))) return fail("pad 17 should be invalid");

    const mrdrums_param_desc_t *global = mrdrums_find_global_param("g_master_vol");
    if (!global) return fail("missing g_master_vol descriptor");
    if (global->max_val != 2.0f) return fail("g_master_vol max expected 2.0");
    const mrdrums_param_desc_t *auto_select = mrdrums_find_global_param("ui_auto_select_pad");
    if (!auto_select) return fail("missing ui_auto_select_pad descriptor");
    if (std::strcmp(auto_select->type, "enum") != 0) return fail("ui_auto_select_pad type expected enum");
    if (!auto_select->options_json || std::strcmp(auto_select->options_json, "[\"off\",\"on\"]") != 0) {
        return fail("ui_auto_select_pad options expected [\"off\",\"on\"]");
    }

    const mrdrums_param_desc_t *pad = mrdrums_find_pad_param("p07_tune");
    if (!pad) return fail("missing p07_tune descriptor");
    if (pad->pad_index != 7) return fail("pad_index expected 7");
    if (std::strcmp(pad->suffix, "tune") != 0) return fail("suffix expected tune");
    if (pad->step != 1.0f) return fail("tune step expected 1.0");

    const mrdrums_param_desc_t *pan = mrdrums_find_pad_param("p01_pan");
    if (!pan) return fail("missing p01_pan descriptor");
    if (pan->step != 0.1f) return fail("pan step expected 0.1");

    const mrdrums_param_desc_t *vol = mrdrums_find_pad_param("p01_vol");
    if (!vol) return fail("missing p01_vol descriptor");
    if (vol->max_val != 2.0f) return fail("vol max expected 2.0");

    const mrdrums_param_desc_t *start = mrdrums_find_pad_param("p01_start");
    if (!start) return fail("missing p01_start descriptor");
    if (start->step != 0.01f) return fail("start step expected 0.01");

    const mrdrums_param_desc_t *decay = mrdrums_find_pad_param("p01_decay_ms");
    if (!decay) return fail("missing p01_decay_ms descriptor");
    if (decay->step != 5.0f) return fail("decay step expected 5.0");

    std::printf("PASS: mrdrums pad key mapping\n");
    return 0;
}
