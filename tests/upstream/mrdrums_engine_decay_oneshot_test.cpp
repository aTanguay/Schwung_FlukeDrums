#include <cstdio>

#include "mrdrums_engine.h"

static int fail(const char *msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main() {
    mrdrums_engine_t engine;
    mrdrums_engine_init(&engine);

    static float long_sample[MRDRUMS_ENGINE_SAMPLE_RATE * 2];
    for (int i = 0; i < MRDRUMS_ENGINE_SAMPLE_RATE * 2; i++) long_sample[i] = 0.7f;

    mrdrums_engine_set_pad_sample(&engine, 1, long_sample, MRDRUMS_ENGINE_SAMPLE_RATE * 2, MRDRUMS_ENGINE_SAMPLE_RATE);
    mrdrums_engine_set_pad_mode(&engine, 1, MRDRUMS_PAD_MODE_ONESHOT);
    mrdrums_engine_set_pad_attack_ms(&engine, 1, 0.0f);
    mrdrums_engine_set_pad_decay_ms(&engine, 1, 10.0f);

    mrdrums_engine_note_on(&engine, MRDRUMS_ENGINE_PAD_NOTE_MIN, 127);
    if (mrdrums_engine_active_voice_count(&engine) <= 0) {
        return fail("expected active voice immediately after note_on");
    }

    float left[2048];
    float right[2048];
    mrdrums_engine_render(&engine, left, right, 2048);

    if (mrdrums_engine_active_voice_count(&engine) != 0) {
        return fail("oneshot voice should decay out within 10ms");
    }

    std::printf("PASS: mrdrums oneshot decay envelope\n");
    return 0;
}
