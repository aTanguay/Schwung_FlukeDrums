#include <cstdio>

#include "mrdrums_engine.h"

static int fail(const char *msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main() {
    mrdrums_engine_t engine;
    mrdrums_engine_init(&engine);

    mrdrums_engine_set_master_vol(&engine, 1.75f);
    if (engine.master_vol != 1.75f) return fail("master volume should allow values above 1.0");
    mrdrums_engine_set_master_vol(&engine, 3.0f);
    if (engine.master_vol != 2.0f) return fail("master volume should clamp to 2.0");

    mrdrums_engine_set_pad_vol(&engine, 1, 1.5f);
    if (engine.pads[0].vol != 1.5f) return fail("pad volume should allow values above 1.0");
    mrdrums_engine_set_pad_vol(&engine, 1, 9.0f);
    if (engine.pads[0].vol != 2.0f) return fail("pad volume should clamp to 2.0");

    std::printf("PASS: mrdrums engine supports up to 200%% volume\n");
    return 0;
}
