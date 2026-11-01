#include <cstdio>

#include "mrdrums_engine.h"

static int fail(const char *msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main() {
    if (mrdrums_engine_note_to_pad(36) != 1) return fail("36 should map to pad 1");
    if (mrdrums_engine_note_to_pad(51) != 16) return fail("51 should map to pad 16");
    if (mrdrums_engine_note_to_pad(35) != 0) return fail("35 should be out of range");
    if (mrdrums_engine_note_to_pad(52) != 0) return fail("52 should be out of range");

    std::printf("PASS: mrdrums note map\n");
    return 0;
}
