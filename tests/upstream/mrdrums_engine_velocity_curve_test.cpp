#include <cmath>
#include <cstdio>

#include "mrdrums_engine.h"

static int fail(const char *msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static float trigger_and_get_gain(mrdrums_engine_t *engine, int vel_curve, int velocity) {
    engine->vel_curve = vel_curve;
    mrdrums_engine_all_notes_off(engine);
    mrdrums_engine_note_on(engine, MRDRUMS_ENGINE_PAD_NOTE_MIN, velocity);

    for (int i = 0; i < MRDRUMS_ENGINE_MAX_VOICES; i++) {
        const mrdrums_voice_t *v = &engine->voices[i];
        if (!v->active || v->pad_index != 1) continue;
        return std::sqrt(v->gain_l * v->gain_l + v->gain_r * v->gain_r);
    }

    return -1.0f;
}

int main() {
    mrdrums_engine_t engine;
    mrdrums_engine_init(&engine);

    static float sample[4096];
    for (int i = 0; i < 4096; i++) sample[i] = 0.5f;

    mrdrums_engine_set_pad_sample(&engine, 1, sample, 4096, MRDRUMS_ENGINE_SAMPLE_RATE);
    mrdrums_engine_set_pad_mode(&engine, 1, MRDRUMS_PAD_MODE_ONESHOT);
    mrdrums_engine_set_pad_chance_pct(&engine, 1, 100.0f);
    mrdrums_engine_set_pad_vol(&engine, 1, 1.0f);
    mrdrums_engine_set_pad_pan(&engine, 1, 0.0f);
    mrdrums_engine_set_pad_rand_pan_amt(&engine, 1, 0.0f);
    mrdrums_engine_set_pad_rand_vol_amt(&engine, 1, 0.0f);
    mrdrums_engine_set_pad_rand_decay_amt(&engine, 1, 0.0f);

    const int velocity = 64;
    float linear = trigger_and_get_gain(&engine, 0, velocity);
    float soft = trigger_and_get_gain(&engine, 1, velocity);
    float hard = trigger_and_get_gain(&engine, 2, velocity);

    if (linear <= 0.0f || soft <= 0.0f || hard <= 0.0f) {
        return fail("expected non-zero gains for all velocity curves");
    }

    if (!(soft > linear + 0.10f)) {
        return fail("soft curve should be noticeably louder than linear at mid velocity");
    }
    if (!(hard < linear - 0.10f)) {
        return fail("hard curve should be noticeably quieter than linear at mid velocity");
    }

    std::printf("PASS: mrdrums velocity curves produce distinct gain response\n");
    return 0;
}
