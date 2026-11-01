#include <cstdio>

#include "mrdrums_engine.h"

static int fail(const char *msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main() {
    mrdrums_engine_t engine;
    mrdrums_engine_init(&engine);

    static float sample[4096];
    for (int i = 0; i < 4096; i++) sample[i] = 0.5f;

    mrdrums_engine_set_pad_sample(&engine, 1, sample, 4096, MRDRUMS_ENGINE_SAMPLE_RATE);
    mrdrums_engine_set_pad_mode(&engine, 1, MRDRUMS_PAD_MODE_ONESHOT);
    mrdrums_engine_set_pad_chance_pct(&engine, 1, 100.0f);

    engine.rand_seed = 123456u;

    mrdrums_engine_note_on(&engine, MRDRUMS_ENGINE_PAD_NOTE_MIN, 100);
    if (engine.rand_seed != 123456u) {
        return fail("rand_seed should remain static after note_on");
    }

    mrdrums_engine_note_on(&engine, MRDRUMS_ENGINE_PAD_NOTE_MIN, 100);
    if (engine.rand_seed != 123456u) {
        return fail("rand_seed should remain static after repeated notes");
    }

    std::printf("PASS: mrdrums rand_seed remains static\n");
    return 0;
}
