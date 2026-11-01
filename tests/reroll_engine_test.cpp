/*
 * Standalone test of the reroll engine (reroll.cpp only — no MrDrums code).
 * Plays the role of the plugin's audio thread: queues commands, polls ready
 * buffers, holds them "in use" for a while, then retires them.
 *
 *   g++ -std=c++14 -pthread -fsanitize=thread -DTEST_ROOT='"/tmp/x"' \
 *       tests/reroll_engine_test.cpp src/dsp/reroll.cpp -Isrc/dsp
 */
#include "reroll.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>
#include <set>
#include <deque>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#ifndef TEST_ROOT
#error "define TEST_ROOT"
#endif

static int g_failures = 0;
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "FAIL line %d: ", __LINE__);                   \
            std::fprintf(stderr, __VA_ARGS__);                                  \
            std::fprintf(stderr, "\n");                                         \
            g_failures++;                                                       \
        }                                                                       \
    } while (0)

/* --- fixture files: header-less "audio" whose length is the file size ---- */
static void write_fake(const std::string &path, int frames) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); std::exit(2); }
    std::vector<short> s((size_t)frames, 1000);
    std::fwrite(s.data(), 2, s.size(), f);
    std::fclose(f);
}

/* Loader: 16-bit mono, 1000 Hz "sample rate" so 1 s == 1000 frames == 2 KB. */
static int fake_loader(const char *path, rr_buffer_t *out, char *err, int err_len) {
    struct stat st;
    if (stat(path, &st) != 0) { std::snprintf(err, err_len, "missing"); return -1; }
    if (std::strstr(path, "corrupt")) { std::snprintf(err, err_len, "corrupt"); return -1; }
    int frames = (int)(st.st_size / 2);
    out->data = (float *)std::calloc((size_t)frames + 1, sizeof(float));
    out->length = frames;
    out->sample_rate = 1000;
    return out->data ? 0 : -1;
}

/* --- the fake "plugin" ---------------------------------------------------- */
struct Pad { float *data = nullptr; std::string path; };
static Pad pads[RR_PADS];
struct Held { float *p; int blocks_left; };
static std::deque<Held> held;   /* retired but "a voice still plays it" */
static long g_blocks = 0;

static void audio_block(rr_ctx_t *c) {
    g_blocks++;
    rr_ready_t r;
    while (rr_poll(c, &r)) {
        CHECK(r.pad >= 1 && r.pad <= RR_PADS, "bad pad %d", r.pad);
        Pad &p = pads[r.pad - 1];
        if (p.data) held.push_back({p.data, 5}); /* pretend a voice holds it 5 blocks */
        p.data = r.buf.data;
        p.path = r.path;
        if (p.data) p.data[0] += 1.0f; /* touch: ASan catches use-after-free here */
    }
    for (auto it = held.begin(); it != held.end();) {
        it->p[0] += 0.5f; /* still "playing" */
        if (--it->blocks_left <= 0 && rr_retire(c, it->p) == 0) it = held.erase(it);
        else ++it;
    }
}

static double now_s() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Wait on wall-clock time, and fail loudly instead of silently moving on. */
static void settle(rr_ctx_t *c) {
    const double kTimeout = 120.0;
    const double start = now_s();
    for (int i = 0;; i++) {
        audio_block(c);
        if (i > 2 && !rr_busy(c)) break;
        if (now_s() - start > kTimeout) {
            std::fprintf(stderr, "FAIL: settle() timed out after %.0f s with work still queued\n", kTimeout);
            std::exit(1);
        }
        usleep(1000);
    }
    for (int i = 0; i < 10; i++) { audio_block(c); usleep(300); }
}

static std::vector<std::string> paths() {
    std::vector<std::string> v;
    for (auto &p : pads) v.push_back(p.path);
    return v;
}

static bool starts_with(const std::string &s, const std::string &pre) { return s.compare(0, pre.size(), pre) == 0; }

static int count_dir(const std::string &dir, const char *suffix) {
    DIR *d = opendir(dir.c_str());
    if (!d) return -1;
    int n = 0;
    size_t sl = std::strlen(suffix);
    while (struct dirent *e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() >= sl && name.compare(name.size() - sl, sl, suffix) == 0) n++;
    }
    closedir(d);
    return n;
}

static void mkdirs(const std::string &d) {
    if (std::system(("mkdir -p '" + d + "'").c_str()) != 0) std::exit(2);
}

int main() {
    const std::string root = TEST_ROOT;
    const std::string pool = root + "/pool";
    const std::string longpool = root + "/longpool";
    const std::string commit_root = pool + "/Schwung/Reroll";

    if (std::system(("rm -rf '" + root + "'").c_str()) != 0) return 2;
    mkdirs(pool + "/kicks"); mkdirs(pool + "/snares/deep"); mkdirs(pool + "/perc");
    mkdirs(commit_root); mkdirs(longpool);

    int short_count = 0;
    for (int i = 0; i < 14; i++) write_fake(pool + "/kicks/k" + std::to_string(i) + ".wav", 300), short_count++;
    for (int i = 0; i < 14; i++) write_fake(pool + "/snares/deep/s" + std::to_string(i) + ".WAV", 500), short_count++;
    for (int i = 0; i < 12; i++) write_fake(pool + "/perc/p" + std::to_string(i) + ".aif", 800), short_count++;
    for (int i = 0; i < 3; i++) write_fake(pool + "/perc/stem" + std::to_string(i) + ".wav", 5000);  /* 5 s */
    write_fake(pool + "/perc/corrupt.wav", 100);           /* decoder fails */
    write_fake(commit_root + "/already-saved.wav", 100);   /* never pooled */
    write_fake(pool + "/perc/._junk.wav", 100);             /* hidden */
    write_fake(pool + "/perc/notes.txt", 100);              /* not audio */
    if (symlink(pool.c_str(), (pool + "/loop").c_str()) != 0) return 2; /* dir symlink */
    for (int i = 0; i < 3; i++) write_fake(longpool + "/long" + std::to_string(i) + ".wav", 5000);
    const int pool_total = short_count + 3 + 1; /* + stems + corrupt */

    rr_ctx_t *c = rr_create(fake_loader, commit_root.c_str());
    CHECK(c != nullptr, "create");
    CHECK(rr_status(c) == RR_ST_NO_POOL, "fresh status");

    /* Scan */
    rr_set_max_len(c, 2.0f);
    rr_set_pool(c, (pool + "///").c_str(), 1);
    settle(c);
    CHECK(rr_pool_count(c) == pool_total, "pool count %d want %d", rr_pool_count(c), pool_total);
    CHECK(rr_status(c) == RR_ST_IDLE, "status %s", rr_status_label(rr_status(c)));

    rr_set_pool(c, pool.c_str(), 0); /* not recursive: no files at the top level */
    settle(c);
    CHECK(rr_pool_count(c) == 0 && rr_status(c) == RR_ST_NO_POOL, "non-recursive top level count %d", rr_pool_count(c));
    rr_set_pool(c, pool.c_str(), 1);
    settle(c);

    /* Undo with nothing to undo: harmless */
    rr_undo(c);
    settle(c);
    for (auto &p : pads) CHECK(p.path.empty() && !p.data, "undo on empty stack loaded something");

    /* Roll all 16 */
    rr_roll(c, 0xFFFF);
    settle(c);
    auto r1 = paths();
    std::set<std::string> u(r1.begin(), r1.end());
    CHECK(u.size() == 16, "roll1 distinct %zu", u.size());
    for (auto &p : r1) {
        CHECK(starts_with(p, pool + "/"), "outside pool: %s", p.c_str());
        CHECK(p.find("stem") == std::string::npos, "too long: %s", p.c_str());
        CHECK(p.find("corrupt") == std::string::npos, "corrupt landed: %s", p.c_str());
        CHECK(p.find("/Schwung/") == std::string::npos, "committed file pooled: %s", p.c_str());
        CHECK(p.find("/loop/") == std::string::npos, "symlink followed: %s", p.c_str());
    }
    for (auto &p : pads) CHECK(p.data != nullptr, "pad without buffer");
    CHECK(rr_undo_depth(c) == 1, "undo depth %d", rr_undo_depth(c));

    /* Lock pads 1 and 5: roll mask excludes them */
    uint32_t unlocked = 0xFFFF & ~(1u << 0) & ~(1u << 4);
    rr_roll(c, unlocked);
    settle(c);
    auto r2 = paths();
    CHECK(r2[0] == r1[0] && r2[4] == r1[4], "locked pads changed");
    int changed = 0;
    for (int i = 0; i < 16; i++) if (i != 0 && i != 4 && r2[i] != r1[i]) changed++;
    CHECK(changed == 14, "changed %d of 14", changed);

    /* Undo */
    rr_undo(c);
    settle(c);
    CHECK(paths() == r1, "undo did not restore roll1");
    CHECK(rr_undo_depth(c) == 1, "undo depth after undo %d (roll1 is still undoable)", rr_undo_depth(c));

    /* Single pad */
    rr_roll(c, 1u << 2);
    settle(c);
    auto r3 = paths();
    for (int i = 0; i < 16; i++) {
        if (i == 2) CHECK(r3[i] != r1[i], "pad 3 didn't change");
        else CHECK(r3[i] == r1[i], "pad %d changed on single roll", i + 1);
    }

    /* Stress: 60 rolls queued at once (command ring is 64) + ring backpressure */
    int queued = 0;
    for (int k = 0; k < 60; k++) queued += rr_roll(c, unlocked) == 0;
    CHECK(queued >= 50, "only %d rolls queued", queued);
    settle(c);
    auto rs = paths();
    CHECK(rs[0] == r1[0] && rs[4] == r1[4], "locks broke under stress");
    CHECK(rr_undo_depth(c) == RR_UNDO_DEPTH, "undo cap %d", rr_undo_depth(c));

    /* Explicit loads: clear, bad path, superseded preview loads */
    rr_load(c, 7, "", 0);
    rr_load(c, 8, (pool + "/perc/corrupt.wav").c_str(), 0);
    for (int i = 0; i < 12; i++) rr_load(c, 9, (pool + "/kicks/k" + std::to_string(i) + ".wav").c_str(), 0);
    settle(c);
    CHECK(pads[6].data == nullptr && pads[6].path.empty(), "clear failed");
    CHECK(pads[7].data == nullptr && pads[7].path == pool + "/perc/corrupt.wav", "corrupt load should clear but keep path");
    CHECK(pads[8].path == pool + "/kicks/k11.wav" && pads[8].data, "last preview load should win: %s", pads[8].path.c_str());
    rr_load(c, 10, (pool + "/perc/stem0.wav").c_str(), 0);
    settle(c);
    CHECK(pads[9].path == pool + "/perc/stem0.wav" && pads[9].data, "explicit load must ignore max_len");

    /* Commit */
    rr_commit(c);
    settle(c);
    CHECK(rr_status(c) == RR_ST_COMMITTED, "commit status %s", rr_status_label(rr_status(c)));
    char name[256];
    CHECK(rr_last_commit_name(c, name, sizeof(name)) > 0 && starts_with(name, "Reroll "), "commit name '%s'", name);
    std::string kit = commit_root + "/" + name;
    int filled = 0;
    for (auto &p : pads) filled += !p.path.empty();
    int copied = count_dir(kit, ".wav") + count_dir(kit, ".WAV") + count_dir(kit, ".aif");
    /* pad 8's path is the corrupt file: it's still a real file, so it is copied too */
    CHECK(copied == filled, "copied %d files, %d pads have paths", copied, filled);
    CHECK(count_dir(kit, "reroll-kit.json") == 1, "manifest missing");
    rr_set_pool(c, pool.c_str(), 1);
    settle(c);
    CHECK(rr_pool_count(c) == pool_total, "commit leaked into pool (%d)", rr_pool_count(c));

    /* A pool where nothing fits: roll is a no-op and leaves no undo entry */
    rr_set_pool(c, longpool.c_str(), 1);
    settle(c);
    auto before = paths();
    int depth = rr_undo_depth(c);
    rr_roll(c, 0xFFFF);
    settle(c);
    CHECK(paths() == before, "too-long pool changed pads");
    CHECK(rr_undo_depth(c) == depth, "no-op roll left undo entry");
    rr_set_max_len(c, 0.0f);
    rr_roll(c, 0xFFFF);
    settle(c);
    int longs = 0;
    for (auto &p : paths()) longs += starts_with(p, longpool + "/");
    CHECK(longs == 16, "max_len 0 should allow long files (%d)", longs);

    /* Missing pool folder */
    rr_set_pool(c, (root + "/nope").c_str(), 1);
    settle(c);
    CHECK(rr_status(c) == RR_ST_NO_POOL && rr_pool_count(c) == 0, "missing pool status");

    /* ---- Folder picker ---- */
    CHECK(rr_folders(c) == nullptr, "folder list before first scan");
    rr_add_folder_root(c, (pool + "/").c_str(), "My");
    rr_add_folder_root(c, pool.c_str(), "Dup");            /* same dir: ignored */
    rr_add_folder_root(c, (root + "/missing").c_str(), "Gone");
    rr_add_folder_root(c, longpool.c_str(), "Core");
    rr_scan_folders(c);
    settle(c);
    const rr_folder_list_t *fl = rr_folders(c);
    CHECK(fl != nullptr, "no folder list after scan");
    if (fl) {
        std::vector<std::string> got;
        for (int i = 0; i < fl->count; i++) got.push_back(fl->e[i].label);
        /* kicks 14, perc 12+3 stems+corrupt = 16, snares/deep 14; Schwung/ holds
         * only the commit folder -> 0 -> not listed; loop symlink not followed */
        std::vector<std::string> want = {"My (44)", "My/kicks (14)", "My/perc (16)", "My/snares (14)",
                                         "My/snares/deep (14)", "Core (3)"};
        CHECK(got == want, "folder list mismatch (got %d entries)", fl->count);
        if (got != want) for (auto &g : got) std::fprintf(stderr, "    got: %s\n", g.c_str());
        CHECK(fl->count > 1 && std::string(fl->e[1].path) == pool + "/kicks", "folder path: %s", fl->e[1].path);
    }

    /* 150 folders under one root: capped at 128, shallow first, still tree-sorted */
    const std::string wide = root + "/wide";
    for (int i = 0; i < 150; i++) {
        char d[64];
        std::snprintf(d, sizeof d, "/f%03d", i);
        mkdirs(wide + d);
        write_fake(wide + d + "/a.wav", 10);
    }
    mkdirs(wide + "/f000/deep1/deep2/deep3/deep4");
    write_fake(wide + "/f000/deep1/deep2/deep3/deep4/z.wav", 10);
    rr_ctx_t *c2 = rr_create(fake_loader, commit_root.c_str());
    rr_add_folder_root(c2, wide.c_str(), "W");
    rr_scan_folders(c2);
    for (int i = 0; i < 5000 && rr_busy(c2); i++) usleep(1000);
    const rr_folder_list_t *w = rr_folders(c2);
    CHECK(w && w->count == RR_MAX_FOLDERS, "wide list count %d", w ? w->count : -1);
    if (w) {
        CHECK(std::string(w->e[0].label) == "W (151)", "wide root label %s", w->e[0].label);
        bool sorted = true, deep = false;
        for (int i = 2; i < w->count; i++) sorted = sorted && strcasecmp(w->e[i - 1].label, w->e[i].label) < 0;
        for (int i = 0; i < w->count; i++) deep = deep || std::strstr(w->e[i].label, "deep") != nullptr;
        CHECK(sorted, "wide list not in tree order");
        CHECK(!deep, "deep folders should lose to shallow ones at the cap");
        std::set<std::string> uniq;
        for (int i = 0; i < w->count; i++) uniq.insert(w->e[i].label);
        CHECK((int)uniq.size() == w->count, "folder labels not unique");
    }

    /* Rescans publish new lists; an old pointer stays valid (ASan checks the read) */
    const rr_folder_list_t *old = rr_folders(c2);
    write_fake(wide + "/f001/b.wav", 10);
    rr_scan_folders(c2);
    rr_scan_folders(c2);    /* coalesced */
    for (int i = 0; i < 5000 && rr_busy(c2); i++) usleep(1000);
    CHECK(rr_folders(c2) != old, "rescan didn't publish a new list");
    CHECK(old && std::string(old->e[0].label) == "W (151)", "old list was modified");
    CHECK(std::string(rr_folders(c2)->e[0].label) == "W (152)", "new count: %s", rr_folders(c2)->e[0].label);
    rr_destroy(c2);

    /* Long names truncate from the left and keep the count */
    const std::string longname(120, 'x');
    mkdirs(pool + "/" + longname);
    write_fake(pool + "/" + longname + "/l.wav", 10);
    rr_scan_folders(c);
    settle(c);
    bool trunc_ok = false;
    for (int i = 0; rr_folders(c) && i < rr_folders(c)->count; i++) {
        std::string l = rr_folders(c)->e[i].label;
        if (l.find("xxxx") != std::string::npos)
            trunc_ok = l.size() < RR_FOLDER_LABEL_MAX && l.compare(0, 3, "...") == 0 && l.find("(1)") != std::string::npos;
    }
    CHECK(trunc_ok, "long folder label not truncated as '...tail (n)'");

    /* Shutdown with work in flight: nothing may leak (LSan) or hang */
    rr_set_pool(c, pool.c_str(), 1);
    for (int k = 0; k < 20; k++) rr_roll(c, 0xFFFF);
    audio_block(c);
    rr_destroy(c);
    for (auto &h : held) std::free(h.p);
    for (auto &p : pads) std::free(p.data);

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("reroll engine: all checks passed (%ld simulated audio blocks)\n", g_blocks);
    return 0;
}
