#include <cstdio>
#include <vector>

#include "mrdrums_engine.h"

static int fail(const char *msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main() {
    mrdrums_engine_t engine;
    mrdrums_engine_init(&engine);

    std::vector<float> sample(8192, 0.25f);
    mrdrums_engine_set_pad_sample(&engine, 1, sample.data(), (int)sample.size(), 44100);
    mrdrums_engine_set_pad_sample(&engine, 2, sample.data(), (int)sample.size(), 44100);
    mrdrums_engine_set_pad_sample(&engine, 3, sample.data(), (int)sample.size(), 44100);
    mrdrums_engine_set_pad_sample(&engine, 4, sample.data(), (int)sample.size(), 44100);

    mrdrums_engine_set_pad_choke_group(&engine, 1, 1);
    mrdrums_engine_set_pad_choke_group(&engine, 2, 1);

    mrdrums_engine_set_pad_mode(&engine, 1, MRDRUMS_PAD_MODE_ONESHOT);
    mrdrums_engine_set_pad_mode(&engine, 2, MRDRUMS_PAD_MODE_ONESHOT);

    mrdrums_engine_note_on(&engine, 36, 100);
    if (mrdrums_engine_active_voice_count(&engine) != 1) return fail("expected one voice after first note");

    mrdrums_engine_note_on(&engine, 37, 100);
    if (mrdrums_engine_active_voice_count(&engine) != 1) return fail("choke should keep one voice");
    if (mrdrums_engine_active_pad_voice_count(&engine, 1) != 0) return fail("pad1 voice should be choked");
    if (mrdrums_engine_active_pad_voice_count(&engine, 2) != 1) return fail("pad2 voice should remain");

    mrdrums_engine_set_pad_mode(&engine, 3, MRDRUMS_PAD_MODE_ONESHOT);
    mrdrums_engine_note_on(&engine, 38, 100);
    mrdrums_engine_note_off(&engine, 38);

    float out_l[256] = {0};
    float out_r[256] = {0};
    mrdrums_engine_render(&engine, out_l, out_r, 256);

    if (mrdrums_engine_active_pad_voice_count(&engine, 3) == 0) {
        return fail("oneshot voice should continue after note off");
    }

    mrdrums_engine_set_pad_mode(&engine, 4, MRDRUMS_PAD_MODE_GATE);
    mrdrums_engine_set_pad_decay_ms(&engine, 4, 10.0f);
    mrdrums_engine_note_on(&engine, 39, 100);
    mrdrums_engine_note_off(&engine, 39);

    for (int i = 0; i < 80; i++) {
        mrdrums_engine_render(&engine, out_l, out_r, 256);
    }

    if (mrdrums_engine_active_pad_voice_count(&engine, 4) != 0) {
        return fail("gate voice should release after note off");
    }

    std::printf("PASS: mrdrums choke/mode behavior\n");
    return 0;
}
