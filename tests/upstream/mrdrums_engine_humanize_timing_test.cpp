#include <cstdio>

#include "mrdrums_engine.h"

static int fail(const char *msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main() {
    mrdrums_engine_t engine;
    mrdrums_engine_init(&engine);

    static float sample[8192];
    for (int i = 0; i < 8192; i++) sample[i] = 0.8f;

    mrdrums_engine_set_pad_sample(&engine, 1, sample, 8192, MRDRUMS_ENGINE_SAMPLE_RATE);
    mrdrums_engine_set_pad_mode(&engine, 1, MRDRUMS_PAD_MODE_ONESHOT);
    mrdrums_engine_set_pad_chance_pct(&engine, 1, 100.0f);
    mrdrums_engine_set_pad_attack_ms(&engine, 1, 0.0f);
    mrdrums_engine_set_pad_decay_ms(&engine, 1, 5000.0f);

    engine.humanize_ms = 50.0f;
    engine.rng_state = 123u; /* Deterministic non-zero timing offset */

    mrdrums_engine_note_on(&engine, MRDRUMS_ENGINE_PAD_NOTE_MIN, 127);

    float left[1] = {0.0f};
    float right[1] = {0.0f};
    mrdrums_engine_render(&engine, left, right, 1);

    if (left[0] != 0.0f || right[0] != 0.0f) {
        return fail("humanize timing should delay voice start");
    }

    float later_left[2048];
    float later_right[2048];
    for (int i = 0; i < 2048; i++) {
        later_left[i] = 0.0f;
        later_right[i] = 0.0f;
    }
    mrdrums_engine_render(&engine, later_left, later_right, 2048);

    int found_audio = 0;
    for (int i = 0; i < 2048; i++) {
        if (later_left[i] != 0.0f || later_right[i] != 0.0f) {
            found_audio = 1;
            break;
        }
    }
    if (!found_audio) {
        return fail("humanized voice should eventually render audio");
    }

    std::printf("PASS: mrdrums humanize timing offset\n");
    return 0;
}
