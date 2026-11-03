/*
 * Host-side workflow test for FlukeDrums: drives the real plugin API from one
 * "audio" thread (this main thread) while the worker runs for real.
 *
 * Build (see tests/run_tests.sh):
 *   g++ -std=c++14 -pthread -DREROLL_COMMIT_ROOT=... tests/reroll_workflow_test.cpp src/dsp/<all .cpp>
 */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <set>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

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

#ifndef TEST_ROOT
#error "define TEST_ROOT"
#endif

static int g_failures = 0;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);      \
            std::fprintf(stderr, __VA_ARGS__);                             \
            std::fprintf(stderr, "\n");                                    \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static plugin_api_v2_t *api;
static long g_blocks = 0;
static int g_note_cycle = 0;

/* ---- fixtures ---------------------------------------------------------- */
static void write_wav(const std::string &path, float seconds, float freq) {
    const int sr = 44100;
    const int n = (int)(seconds * sr);
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); std::exit(2); }
    auto u32 = [&](unsigned v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](unsigned short v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + n * 2); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1); u32(sr); u32(sr * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(n * 2);
    for (int i = 0; i < n; i++) {
        float env = 1.0f - (float)i / (float)n;
        short s = (short)(std::sin(2.0 * M_PI * freq * i / sr) * env * 20000.0);
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
}

/* Constant full-scale sine, for driving the output stage into its clipper. */
static void write_loud_wav(const std::string &path, float seconds, float freq) {
    const int sr = 44100;
    const int n = (int)(seconds * sr);
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); std::exit(2); }
    auto u32 = [&](unsigned v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](unsigned short v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + n * 2); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1); u32(sr); u32(sr * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(n * 2);
    for (int i = 0; i < n; i++) {
        short s = (short)(std::sin(2.0 * M_PI * freq * i / sr) * 32767.0);
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
}

static void write_text(const std::string &path, const std::string &text) {
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f) { std::perror(path.c_str()); std::exit(2); }
    std::fputs(text.c_str(), f);
    std::fclose(f);
}

static void mkdirs(const std::string &d) { std::string cmd = "mkdir -p '" + d + "'"; if (std::system(cmd.c_str())) std::exit(2); }

/* ---- plugin helpers ---------------------------------------------------- */
static std::string get(void *inst, const char *key) {
    static char buf[65536];
    buf[0] = 0;
    int n = api->get_param(inst, key, buf, (int)sizeof(buf));
    return n < 0 ? std::string("<err>") : std::string(buf);
}

static void set(void *inst, const char *key, const char *val) { api->set_param(inst, key, val); }

static std::string err(void *inst) {
    char buf[512];
    buf[0] = 0;
    api->get_error(inst, buf, (int)sizeof(buf));
    return buf;
}

/* A file from the pool that isn't in either kit. */
static std::string spare_file(const std::string &pool, const std::vector<std::string> &a, const std::vector<std::string> &b) {
    for (int i = 0; i < 12; i++) {
        std::string cand = pool + "/perc/p" + std::to_string(i) + ".wav";
        if (std::find(a.begin(), a.end(), cand) == a.end() && std::find(b.begin(), b.end(), cand) == b.end()) return cand;
    }
    for (int i = 0; i < 14; i++) {
        std::string cand = pool + "/kicks/k" + std::to_string(i) + ".wav";
        if (std::find(a.begin(), a.end(), cand) == a.end() && std::find(b.begin(), b.end(), cand) == b.end()) return cand;
    }
    return "";
}

/* One audio block. Also hammers pads so swaps happen under playing voices. */
static float render(void *inst) {
    short out[128 * 2];
    if ((g_blocks++ % 3) == 0) {
        unsigned char on[3] = {0x90, (unsigned char)(36 + (g_note_cycle++ % 16)), 110};
        api->on_midi(inst, on, 3, 0);
    }
    api->render_block(inst, out, 128);
    float peak = 0.0f;
    for (int i = 0; i < 256; i++) peak = std::fmax(peak, std::fabs((float)out[i]));
    return peak;
}

static double now_s() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Run blocks until the worker has drained. Waits on wall-clock time, not a
 * block count: sanitizers (TSan especially) slow decoding 5-20x, and a
 * silent cap here once made a slow-but-correct roll look like a logic bug. */
static void settle(void *inst) {
    const double kTimeout = 120.0;
    const double start = now_s();
    for (int i = 0;; i++) {
        render(inst);
        if (i > 4 && get(inst, "rr_busy") == "0") break;
        if (now_s() - start > kTimeout) {
            std::fprintf(stderr, "FAIL: settle() timed out after %.0f s with work still queued\n", kTimeout);
            std::exit(1);
        }
        usleep(1000);
    }
    for (int i = 0; i < 8; i++) { render(inst); usleep(500); }
}

static std::vector<std::string> pad_paths(void *inst) {
    std::vector<std::string> v;
    for (int p = 1; p <= 16; p++) {
        char key[32];
        std::snprintf(key, sizeof(key), "p%02d_sample_path", p);
        v.push_back(get(inst, key));
    }
    return v;
}

static bool starts_with(const std::string &s, const std::string &pre) { return s.compare(0, pre.size(), pre) == 0; }

static int count_files(const std::string &dir, const char *ext) {
    DIR *d = opendir(dir.c_str());
    if (!d) return -1;
    int n = 0;
    while (struct dirent *e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() > std::strlen(ext) && name.compare(name.size() - std::strlen(ext), std::strlen(ext), ext) == 0) n++;
    }
    closedir(d);
    return n;
}

int main() {
    const std::string root = TEST_ROOT;
    const std::string pool = root + "/pool";
    const std::string longpool = root + "/longpool";
    const std::string commit_root = root + "/pool/Schwung/FlukeDrums"; /* inside the pool on purpose */

    std::system(("rm -rf '" + root + "'").c_str());
    mkdirs(pool + "/kicks"); mkdirs(pool + "/snares/deep"); mkdirs(pool + "/perc"); mkdirs(commit_root);
    mkdirs(longpool);

    int short_count = 0;
    for (int i = 0; i < 14; i++) write_wav(pool + "/kicks/k" + std::to_string(i) + ".wav", 0.15f, 50 + i), short_count++;
    for (int i = 0; i < 14; i++) write_wav(pool + "/snares/deep/s" + std::to_string(i) + ".WAV", 0.25f, 200 + i), short_count++;
    for (int i = 0; i < 12; i++) write_wav(pool + "/perc/p" + std::to_string(i) + ".wav", 0.4f, 800 + i * 10), short_count++;
    for (int i = 0; i < 3; i++) write_wav(pool + "/perc/stem" + std::to_string(i) + ".wav", 5.0f, 100); /* > max_len */
    write_wav(commit_root + "/already-saved.wav", 0.1f, 440);   /* must NOT be pooled */
    write_wav(pool + "/perc/._mac-junk.wav", 0.1f, 440);         /* hidden: skipped */
    std::FILE *junk = std::fopen((pool + "/perc/readme.txt").c_str(), "w"); std::fputs("hi", junk); std::fclose(junk);
    symlink(pool.c_str(), (pool + "/loop").c_str());              /* dir symlink: not followed */
    for (int i = 0; i < 3; i++) write_wav(longpool + "/long" + std::to_string(i) + ".wav", 5.0f, 100);

    host_api_v1_t host;
    std::memset(&host, 0, sizeof(host));
    host.api_version = 1;
    host.sample_rate = 44100;
    host.frames_per_block = 128;
    api = move_plugin_init_v2(&host);

    std::string init_json = "{\"rr_pool_dir\":\"" + pool + "\",\"rr_max_len\":2}";
    void *inst = api->create_instance(root.c_str(), init_json.c_str());
    CHECK(inst != nullptr, "create_instance");
    settle(inst);

    /* 1. Scan */
    CHECK(get(inst, "rr_pool_count") == std::to_string(short_count + 3),
          "pool count %s, want %d (40 short + 3 stems; commit dir, hidden, txt, symlink excluded)",
          get(inst, "rr_pool_count").c_str(), short_count + 3);
    CHECK(starts_with(get(inst, "rr_status"), "Ready"), "status after scan: %s", get(inst, "rr_status").c_str());

    /* 2. Trigger safety: "do nothing" values must not roll */
    set(inst, "rr_roll", "-");
    set(inst, "rr_roll", "0");
    settle(inst);
    CHECK(get(inst, "rr_undo_depth") == "0", "dash/0 must not fire a roll");
    for (auto &p : pad_paths(inst)) CHECK(p.empty(), "pad loaded without a roll: %s", p.c_str());

    /* 3. First roll: 16 distinct short samples from the pool */
    set(inst, "rr_roll", "Roll!");
    settle(inst);
    std::vector<std::string> roll1 = pad_paths(inst);
    std::set<std::string> uniq(roll1.begin(), roll1.end());
    CHECK(uniq.size() == 16, "roll1 has %zu distinct paths, want 16", uniq.size());
    for (auto &p : roll1) {
        CHECK(starts_with(p, pool + "/"), "roll1 path outside pool: %s", p.c_str());
        CHECK(p.find("/stem") == std::string::npos, "roll1 picked a too-long stem: %s", p.c_str());
        CHECK(p.find("/Schwung/FlukeDrums/") == std::string::npos, "roll1 picked a committed file: %s", p.c_str());
        CHECK(p.find("/loop/") == std::string::npos, "roll1 followed the dir symlink: %s", p.c_str());
    }
    CHECK(get(inst, "rr_undo_depth") == "1", "undo depth after roll1: %s", get(inst, "rr_undo_depth").c_str());

    /* 3b. Kit Mode (on by default): pads 1-8 follow the kit by name. The pool
     * has kicks/, snares/deep/ and perc/ (named by folder); no hats, claps,
     * cymbals or toms, so clap -> snare and tom -> perc, hats and cymbal random. */
    CHECK(get(inst, "rr_kit_mode") == "on", "kit mode default: %s", get(inst, "rr_kit_mode").c_str());
    CHECK(get(inst, "rr_kit_parts") == "5/8", "kit parts: %s", get(inst, "rr_kit_parts").c_str());
    CHECK(starts_with(roll1[0], pool + "/kicks/") && starts_with(roll1[1], pool + "/kicks/"), "pads 1-2 not kicks");
    CHECK(starts_with(roll1[2], pool + "/snares/") && starts_with(roll1[3], pool + "/snares/"), "pads 3-4 not snares");
    CHECK(starts_with(roll1[7], pool + "/perc/"), "pad 8 not perc: %s", roll1[7].c_str());
    {
        std::string cpk = get(inst, "chain_params");
        CHECK(cpk.find("\"rr_kit_mode\"") != std::string::npos && cpk.find("\"rr_kit_parts\"") != std::string::npos,
              "chain_params missing Kit Mode / Kit Parts");
        std::string hk = get(inst, "ui_hierarchy");
        CHECK(hk.find("\"knobs\":[\"rr_kit_mode\"") != std::string::npos, "Kit Mode isn't knob 1 on the Pool page");
    }

    float peak = 0.0f;
    for (int i = 0; i < 60; i++) peak = std::fmax(peak, render(inst));
    CHECK(peak > 1000.0f, "no audio after roll (peak %.0f)", peak);

    /* 4. Lock pads 1 and 5 (5 via the current-pad alias), roll again */
    set(inst, "p01_lock", "on");
    set(inst, "ui_auto_select_pad", "off");
    set(inst, "ui_current_pad", "5");
    set(inst, "pad_lock", "on");
    CHECK(get(inst, "p05_lock") == "on", "pad_lock alias didn't reach p05_lock");
    set(inst, "rr_roll", "1");
    settle(inst);
    std::vector<std::string> roll2 = pad_paths(inst);
    CHECK(roll2[0] == roll1[0], "locked pad 1 changed: %s -> %s", roll1[0].c_str(), roll2[0].c_str());
    CHECK(roll2[4] == roll1[4], "locked pad 5 changed");
    int changed = 0;
    for (int i = 0; i < 16; i++) if (i != 0 && i != 4 && roll2[i] != roll1[i]) changed++;
    CHECK(changed == 14, "unlocked pads changed: %d of 14", changed);

    /* 5. Undo puts roll1 back exactly */
    set(inst, "rr_undo", "Undo!");
    settle(inst);
    CHECK(pad_paths(inst) == roll1, "undo didn't restore roll1");
    CHECK(get(inst, "rr_undo_depth") == "1", "undo depth after undo: %s (roll1 stays undoable)", get(inst, "rr_undo_depth").c_str());

    /* 5b. Undo leaves pads locked since the roll, and hand-picked pads, alone */
    {
        set(inst, "rr_roll", "Roll!");
        settle(inst);
        std::vector<std::string> rb = pad_paths(inst);
        set(inst, "p06_lock", "on");                     /* liked what the roll gave pad 6 */
        std::string pick = spare_file(pool, rb, roll1);
        CHECK(!pick.empty(), "no spare file for the hand-pick");
        set(inst, "p07_sample_path", pick.c_str());      /* replaced pad 7 by hand */
        settle(inst);
        CHECK(get(inst, "rr_undo_depth") == "2", "hand-pick changed undo depth: %s", get(inst, "rr_undo_depth").c_str());
        set(inst, "rr_undo", "Undo!");
        settle(inst);
        std::vector<std::string> ru = pad_paths(inst);
        CHECK(ru[5] == rb[5], "undo reverted locked pad 6");
        CHECK(ru[6] == pick, "undo clobbered hand-picked pad 7: %s", ru[6].c_str());
        for (int i = 0; i < 16; i++)
            if (i != 5 && i != 6) CHECK(ru[i] == roll1[i], "undo didn't restore pad %d", i + 1);
        CHECK(get(inst, "rr_undo_depth") == "1", "undo depth after 5b: %s", get(inst, "rr_undo_depth").c_str());

        set(inst, "p06_lock", "off");
        set(inst, "p06_sample_path", roll1[5].c_str());
        set(inst, "p07_sample_path", roll1[6].c_str());
        settle(inst);
        CHECK(pad_paths(inst) == roll1, "couldn't put roll1 back after 5b");
    }

    /* 6. Roll Pad: only the current pad (3) changes */
    set(inst, "ui_current_pad", "3");
    set(inst, "rr_roll_pad", "Roll!");
    settle(inst);
    std::vector<std::string> roll3 = pad_paths(inst);
    for (int i = 0; i < 16; i++) {
        if (i == 2) CHECK(roll3[i] != roll1[i], "roll_pad didn't change pad 3");
        else CHECK(roll3[i] == roll1[i], "roll_pad changed pad %d", i + 1);
    }
    CHECK(starts_with(roll3[2], pool + "/snares/"), "Roll Pad on the snare pad gave %s", roll3[2].c_str());
    /* ...and a locked current pad is left alone */
    set(inst, "ui_current_pad", "5");
    set(inst, "rr_roll_pad", "Roll!");
    settle(inst);
    CHECK(pad_paths(inst)[4] == roll1[4], "roll_pad rolled a locked pad");

    /* 7. Stress: many rolls back-to-back while playing (ring backpressure, retire path) */
    for (int k = 0; k < 40; k++) {
        set(inst, "rr_roll", "Roll!");
        for (int i = 0; i < 3; i++) render(inst);
    }
    settle(inst);
    std::vector<std::string> stressed = pad_paths(inst);
    CHECK(stressed[0] == roll1[0] && stressed[4] == roll1[4], "locks broke under stress");
    CHECK(get(inst, "rr_undo_depth") == "32", "undo depth caps at 32, got %s", get(inst, "rr_undo_depth").c_str());

    /* 7b. Kit Mode off: pad 2 is no longer always a kick (14 of 43 files are) */
    set(inst, "rr_kit_mode", "off");
    CHECK(get(inst, "rr_kit_mode") == "off", "kit mode off: %s", get(inst, "rr_kit_mode").c_str());
    {
        int non_kick = 0;
        for (int k = 0; k < 10; k++) {
            set(inst, "rr_roll", "Roll!");
            settle(inst);
            non_kick += !starts_with(pad_paths(inst)[1], pool + "/kicks/");
        }
        CHECK(non_kick > 0, "kit mode off still always rolled a kick on pad 2");
    }
    set(inst, "rr_kit_mode", "on");

    /* 8. Commit: 16 files + manifest, never re-pooled */
    set(inst, "rr_commit", "Save!");
    settle(inst);
    std::string status = get(inst, "rr_status");
    std::string kit = get(inst, "rr_last_kit");
    CHECK(starts_with(status, "Saved: Fluke "), "status after commit: %s", status.c_str());
    CHECK(starts_with(kit, "Fluke "), "last kit name: %s", kit.c_str());
    std::string kitdir = commit_root + "/" + kit;
    CHECK(count_files(kitdir, ".wav") + count_files(kitdir, ".WAV") == 16, "kit folder has %d wavs",
          count_files(kitdir, ".wav") + count_files(kitdir, ".WAV"));
    struct stat st;
    CHECK(stat((kitdir + "/flukedrums-kit.json").c_str(), &st) == 0 && st.st_size > 100, "manifest missing");
    {
        std::string man;
        FILE *mf = std::fopen((kitdir + "/flukedrums-kit.json").c_str(), "r");
        if (mf) { char b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, mf)) > 0) man.append(b, n); std::fclose(mf); }
        /* pad 1 is locked to roll1's kick */
        CHECK(man.find("\"kind\": \"kick\"") != std::string::npos, "manifest doesn't name pad kinds");
    }
    set(inst, "rr_rescan", "Scan!");
    settle(inst);
    CHECK(get(inst, "rr_pool_count") == std::to_string(short_count + 3), "commit leaked into pool: %s",
          get(inst, "rr_pool_count").c_str());

    /* 9. Picking a FILE as the pool keeps its folder */
    set(inst, "rr_pool_dir", (pool + "/kicks/k3.wav").c_str());
    settle(inst);
    CHECK(get(inst, "rr_pool_dir") == pool + "/kicks", "pool dir from file pick: %s", get(inst, "rr_pool_dir").c_str());
    CHECK(get(inst, "rr_pool_count") == "14", "kicks-only pool count %s", get(inst, "rr_pool_count").c_str());
    set(inst, "rr_unlock_all", "Clear!");
    set(inst, "rr_roll", "Roll!");
    settle(inst);
    for (auto &p : pad_paths(inst)) CHECK(starts_with(p, pool + "/kicks/"), "kicks pool gave %s", p.c_str());

    /* 10. A pool where nothing fits max_len: roll is a no-op, not a hang */
    std::vector<std::string> before_long = pad_paths(inst);
    int depth_before = std::atoi(get(inst, "rr_undo_depth").c_str());
    set(inst, "rr_pool_dir", longpool.c_str());
    settle(inst);
    set(inst, "rr_roll", "Roll!");
    settle(inst);
    CHECK(pad_paths(inst) == before_long, "too-long pool changed pads");
    CHECK(std::atoi(get(inst, "rr_undo_depth").c_str()) == depth_before, "no-op roll left an undo entry");
    /* max_len 0 = no limit: now they load */
    set(inst, "rr_max_len", "0");
    set(inst, "rr_roll", "Roll!");
    settle(inst);
    int longs = 0;
    for (auto &p : pad_paths(inst)) longs += starts_with(p, longpool + "/");
    CHECK(longs == 16, "max_len 0 should allow long files (%d/16)", longs);

    /* 11. Manual pick (browser) still works and goes async */
    set(inst, "ui_current_pad", "2");
    set(inst, "pad_sample_path", (pool + "/perc/p7.wav").c_str());
    settle(inst);
    CHECK(get(inst, "p02_sample_path") == pool + "/perc/p7.wav", "manual pick: %s", get(inst, "p02_sample_path").c_str());

    /* 11b. Folder picker (roots: My = pool, Core = longpool via -D in the build).
     * Expected tree: kicks 14, perc 15 (12 + 3 stems), snares 14 (all in
     * snares/deep). Schwung/ only holds the commit folder, so it's excluded;
     * the loop symlink isn't followed; hidden + .txt files don't count. */
    {
        std::string cp0 = get(inst, "chain_params");
        const char *want[] = {"My (43)", "My/kicks (14)", "My/perc (15)", "My/snares (14)",
                              "My/snares/deep (14)", "Core (3)"};
        size_t last = 0;
        for (const char *w : want) {
            size_t at = cp0.find(std::string("\"") + w + "\"");
            CHECK(at != std::string::npos, "folder list missing \"%s\"", w);
            CHECK(at == std::string::npos || at > last, "folder \"%s\" out of tree order", w);
            if (at != std::string::npos) last = at;
        }
        CHECK(cp0.find("Schwung") == std::string::npos, "commit folder leaked into the folder list");
        CHECK(cp0.find("\"My/loop") == std::string::npos, "symlinked folder listed");

        /* current pool (longpool) IS a listed root */
        CHECK(get(inst, "rr_folder") == "Core (3)", "rr_folder for longpool: %s", get(inst, "rr_folder").c_str());

        /* pick a folder by name: pool follows */
        set(inst, "rr_folder", "My/kicks (14)");
        settle(inst);
        CHECK(get(inst, "rr_pool_dir") == pool + "/kicks", "folder pick -> pool %s", get(inst, "rr_pool_dir").c_str());
        CHECK(get(inst, "rr_pool_count") == "14", "kicks pool count %s", get(inst, "rr_pool_count").c_str());
        CHECK(get(inst, "rr_folder") == "My/kicks (14)", "rr_folder echo: %s", get(inst, "rr_folder").c_str());

        /* index fallback + garbage are handled */
        set(inst, "rr_folder", "4");                     /* index 4 = My/snares/deep */
        settle(inst);
        CHECK(get(inst, "rr_pool_dir") == pool + "/snares/deep", "index pick: %s", get(inst, "rr_pool_dir").c_str());
        set(inst, "rr_folder", "Scanning...");
        set(inst, "rr_folder", "No Such (9)");
        settle(inst);
        CHECK(get(inst, "rr_pool_dir") == pool + "/snares/deep", "garbage folder value moved the pool");

        /* Up One walks up and stops at the library root */
        set(inst, "rr_pool_up", "Up!");
        settle(inst);
        CHECK(get(inst, "rr_pool_dir") == pool + "/snares", "up 1: %s", get(inst, "rr_pool_dir").c_str());
        set(inst, "rr_pool_up", "Up!");
        set(inst, "rr_pool_up", "Up!");
        set(inst, "rr_pool_up", "-");                    /* no-op trigger value */
        settle(inst);
        CHECK(get(inst, "rr_pool_dir") == pool, "up should stop at the My root: %s", get(inst, "rr_pool_dir").c_str());
        CHECK(get(inst, "rr_pool_count") == "43", "root pool count %s", get(inst, "rr_pool_count").c_str());

        /* spinning the knob: many pool changes queued, last one wins, no pile-up */
        const char *spin[] = {"My/kicks (14)", "My/perc (15)", "My/snares (14)", "Core (3)", "My/perc (15)"};
        for (int k = 0; k < 6; k++)
            for (const char *s : spin) set(inst, "rr_folder", s);
        settle(inst);
        CHECK(get(inst, "rr_pool_dir") == pool + "/perc", "spin: last pick should win: %s", get(inst, "rr_pool_dir").c_str());
        CHECK(get(inst, "rr_pool_count") == "15", "spin: perc count %s", get(inst, "rr_pool_count").c_str());

        /* a folder outside the list shows as Custom and is offered first */
        mkdirs(root + "/elsewhere/odd");
        write_wav(root + "/elsewhere/odd/x.wav", 0.1f, 300);
        set(inst, "rr_pool_dir", (root + "/elsewhere/odd/x.wav").c_str());
        settle(inst);
        CHECK(get(inst, "rr_folder") == "Custom: odd", "custom label: %s", get(inst, "rr_folder").c_str());
        std::string cp1 = get(inst, "chain_params");
        size_t fo = cp1.find("\"key\":\"rr_folder\"");
        CHECK(fo != std::string::npos && cp1.find("\"options\":[\"Custom: odd\"", fo) != std::string::npos,
              "custom pool should be the first Folder option");

        /* Rescan picks up a new folder */
        mkdirs(pool + "/fresh");
        write_wav(pool + "/fresh/f1.wav", 0.1f, 500);
        write_wav(pool + "/fresh/f2.wav", 0.1f, 600);
        set(inst, "rr_rescan", "Scan!");
        settle(inst);
        std::string cp2 = get(inst, "chain_params");
        CHECK(cp2.find("\"My/fresh (2)\"") != std::string::npos, "rescan didn't list new folder");
        CHECK(cp2.find("\"My (45)\"") != std::string::npos, "rescan didn't update root count");

        set(inst, "rr_pool_dir", longpool.c_str());   /* back to what step 12 expects */
        settle(inst);
    }

    /* 12. State round-trip into a fresh instance */
    set(inst, "p09_lock", "on");
    set(inst, "rr_kit_mode", "off");
    std::string state = get(inst, "state");
    std::vector<std::string> saved = pad_paths(inst);
    api->destroy_instance(inst);

    void *inst2 = api->create_instance(root.c_str(), state.c_str());
    settle(inst2);
    CHECK(pad_paths(inst2) == saved, "state restore lost pad paths");
    CHECK(get(inst2, "p09_lock") == "on", "state restore lost lock");
    CHECK(get(inst2, "rr_kit_mode") == "off", "state restore lost kit mode: %s", get(inst2, "rr_kit_mode").c_str());
    CHECK(get(inst2, "rr_pool_dir") == longpool, "state restore lost pool: %s", get(inst2, "rr_pool_dir").c_str());
    peak = 0.0f;
    for (int i = 0; i < 60; i++) peak = std::fmax(peak, render(inst2));
    CHECK(peak > 1000.0f, "no audio after restore (peak %.0f)", peak);
    CHECK(get(inst2, "rr_folder") == "Core (3)", "folder after restore: %s", get(inst2, "rr_folder").c_str());

    /* 12b. Move kit presets: parsed on the worker, applied on the audio thread */
    {
        mkdirs(root + "/presets/rel");
        write_wav(root + "/presets/rel/snare one.wav", 0.2f, 330);
        auto preset = [&](const std::string &kick_uri) {
            return std::string("{ \"kind\": \"instrumentRack\", \"chains\": [ { \"devices\": [ {\n"
                               "  \"kind\": \"drumRack\", \"chains\": [\n"
                               "  { \"drumZoneSettings\": { \"receivingNote\": 36, \"chokeGroup\": null },\n"
                               "    \"devices\": [ { \"kind\": \"drumCell\", \"parameters\": { \"Volume\": 0 },\n"
                               "      \"deviceData\": { \"sampleUri\": \"") + kick_uri + "\" } } ] },\n"
                               "  { \"drumZoneSettings\": { \"receivingNote\": 37, \"chokeGroup\": 2 },\n"
                               "    \"devices\": [ { \"kind\": \"drumCell\", \"parameters\": { \"Pan\": 0.5, \"Volume\": -12 },\n"
                               "      \"deviceData\": { \"sampleUri\": \"rel/snare%20one.wav\" } } ] }\n"
                               "] } ] } ] }\n";
        };
        const std::string kit_a = root + "/presets/A.ablpreset";
        const std::string kit_b = root + "/presets/B.ablpreset";
        write_text(kit_a, preset(pool + "/kicks/k1.wav"));
        write_text(kit_b, preset(pool + "/kicks/k2.wav"));

        set(inst2, "p01_vol", "0.5");
        set(inst2, "ui_preset_path", kit_a.c_str());
        /* set_param returned without reading the file: nothing applied yet */
        CHECK(get(inst2, "p01_vol") == "0.5000", "preset applied synchronously (p01_vol %s)", get(inst2, "p01_vol").c_str());
        settle(inst2);
        CHECK(get(inst2, "p01_sample_path") == pool + "/kicks/k1.wav", "preset pad 1: %s", get(inst2, "p01_sample_path").c_str());
        CHECK(get(inst2, "p01_vol") == "2.0000", "preset pad 1 vol: %s", get(inst2, "p01_vol").c_str());
        CHECK(get(inst2, "p02_sample_path") == root + "/presets/rel/snare one.wav", "preset relative uri: %s",
              get(inst2, "p02_sample_path").c_str());
        CHECK(get(inst2, "p02_pan") == "0.5000" && get(inst2, "p02_choke_group") == "2", "preset pad 2 params");
        CHECK(err(inst2).empty(), "preset error: %s", err(inst2).c_str());
        peak = 0.0f;
        for (int i = 0; i < 60; i++) peak = std::fmax(peak, render(inst2));
        CHECK(peak > 1000.0f, "no audio after preset (peak %.0f)", peak);

        /* browsing presets: one load per cursor step, the last one wins */
        for (int k = 0; k < 4; k++) {
            set(inst2, "ui_preset_path", kit_a.c_str());
            set(inst2, "ui_preset_path", kit_b.c_str());
        }
        settle(inst2);
        CHECK(get(inst2, "p01_sample_path") == pool + "/kicks/k2.wav", "preset browse: %s", get(inst2, "p01_sample_path").c_str());

        /* broken presets report an error and change nothing */
        std::vector<std::string> before_bad = pad_paths(inst2);
        write_text(root + "/presets/bad.ablpreset", "{ \"kind\": \"drumRack\", \"chains\": [ ");
        set(inst2, "ui_preset_path", (root + "/presets/bad.ablpreset").c_str());
        settle(inst2);
        CHECK(!err(inst2).empty(), "broken preset gave no error");
        set(inst2, "ui_preset_path", (root + "/presets/missing.ablpreset").c_str());
        settle(inst2);
        CHECK(err(inst2) == "Could not open preset file", "missing preset error: %s", err(inst2).c_str());
        CHECK(pad_paths(inst2) == before_bad, "a failed preset changed pads");
    }

    /* 12c. Output soft clip: driven hard, the output saturates smoothly. The
     * old clipper dropped 0.951 to 0.740, a ~6900-step cliff on every peak. */
    {
        write_loud_wav(root + "/loud.wav", 0.5f, 50.0f);
        void *inst3 = api->create_instance(root.c_str(), "{}");
        set(inst3, "p01_sample_path", (root + "/loud.wav").c_str());
        set(inst3, "p01_vol", "2");
        set(inst3, "p01_decay_ms", "2000");
        set(inst3, "g_master_vol", "2");
        settle(inst3);
        set(inst3, "all_notes_off", "1");
        short out[128 * 2];
        for (int i = 0; i < 2000; i++) {   /* let anything still sounding finish */
            api->render_block(inst3, out, 128);
            bool silent = true;
            for (int s = 0; s < 256; s++) silent = silent && out[s] == 0;
            if (silent) break;
        }
        unsigned char on[3] = {0x90, 36, 127};
        api->on_midi(inst3, on, 3, 0);
        int peak3 = 0, max_step = 0, prev = 0;
        for (int b = 0; b < 100; b++) {
            api->render_block(inst3, out, 128);
            for (int s = 0; s < 128; s++) {
                int v = out[s * 2];
                peak3 = std::max(peak3, std::abs(v));
                max_step = std::max(max_step, std::abs(v - prev));
                prev = v;
            }
        }
        CHECK(peak3 > 31000, "clipper never engaged (peak %d)", peak3);
        CHECK(max_step < 2500, "output jumped %d in one sample: clipper isn't smooth", max_step);
        api->destroy_instance(inst3);
    }

    /* 13. UI contracts are valid JSON-ish and under the 64 KB param limit */
    std::string cp = get(inst2, "chain_params");
    std::string hier = get(inst2, "ui_hierarchy");
    CHECK(cp.size() < 64 * 1024, "chain_params %zu bytes", cp.size());
    CHECK(cp.find("\"rr_roll\"") != std::string::npos && cp.find("\"access\":\"write\"") != std::string::npos,
          "chain_params missing triggers");
    std::FILE *out = std::fopen((root + "/chain_params.json").c_str(), "w"); std::fputs(cp.c_str(), out); std::fclose(out);
    out = std::fopen((root + "/ui_hierarchy.json").c_str(), "w"); std::fputs(hier.c_str(), out); std::fclose(out);

    /* 14. Destroy while work is in flight must not leak or hang */
    for (int k = 0; k < 10; k++) set(inst2, "rr_roll", "Roll!");
    render(inst2);
    api->destroy_instance(inst2);

    if (g_failures) {
        std::fprintf(stderr, "\n%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("reroll workflow: all checks passed (%ld audio blocks)\n", g_blocks);
    return 0;
}
