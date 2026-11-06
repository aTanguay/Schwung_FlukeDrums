/*
 * reroll.cpp — see reroll.h for the threading contract.
 *
 * MIT — the reroll engine of the FlukeDrums module (fork of MrDrums by handcraftedcc).
 */
#include "reroll.h"

#include <algorithm>
#include <atomic>
#include <new>
#include <string>
#include <vector>
#include <array>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* Lock-free single-producer / single-consumer ring.                         */
/* push() is called by exactly one thread, pop()/peek() by exactly one other.*/
/* ------------------------------------------------------------------------- */
template <typename T, size_t N>
class Spsc {
public:
    bool push(const T &v) {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) % N;
        if (next == tail_.load(std::memory_order_acquire)) return false;
        items_[h] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T &out) {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;
        out = items_[t];
        tail_.store((t + 1) % N, std::memory_order_release);
        return true;
    }
    /* Consumer-side scan of what is still queued (slots the producer won't touch). */
    template <typename F>
    bool any_queued(F pred) const {
        size_t t = tail_.load(std::memory_order_relaxed);
        const size_t h = head_.load(std::memory_order_acquire);
        while (t != h) {
            if (pred(items_[t])) return true;
            t = (t + 1) % N;
        }
        return false;
    }

private:
    T items_[N];
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

enum CmdType {
    CMD_SET_POOL = 1,
    CMD_MAX_LEN,
    CMD_ROLL,
    CMD_LOAD,
    CMD_UNDO,
    CMD_COMMIT,
    CMD_FOLDER_ROOT,
    CMD_SCAN_FOLDERS,
    CMD_JOB,
    CMD_KIT_MODE
};

struct Cmd {
    int type;
    int pad;
    uint32_t mask;
    int flag;
    float f;
    rr_job_fn job;
    char path[RR_PATH_MAX];
    char label[32];
};

struct FolderRoot {
    std::string dir;
    std::string label;
};

typedef std::array<std::string, RR_PADS> Snapshot;

/* One undoable change. Keeping "after" lets undo tell which pads the change
 * actually touched, and whether you've replaced them since. */
struct UndoStep {
    Snapshot before;
    Snapshot after;
};

struct rr_ctx {
    rr_loader_fn loader = nullptr;
    char commit_root[RR_PATH_MAX] = {0};

    pthread_t thread{};
    bool thread_ok = false;
    std::atomic<bool> stop{false};

    Spsc<Cmd, 64> cmds;
    Spsc<rr_ready_t, 48> ready;
    Spsc<void *, 256> retired;     /* sample buffers and job results, freed on the worker */
    Spsc<void *, 8> jobs_done;     /* job results, worker -> audio */

    std::atomic<int> status{RR_ST_NO_POOL};
    std::atomic<int> pool_count{0};
    /* files per role after the last scan; zeroed by new rr_ctx() */
    std::atomic<int> role_counts[RR_ROLE_COUNT];
    std::atomic<int> undo_depth{0};
    std::atomic<int> pending{0};

    /* last-commit name: worker writes the idle half, then flips the index. */
    char commit_names[2][RR_PATH_MAX] = {{0}, {0}};
    std::atomic<int> commit_name_idx{-1};

    /* Folder picker. Each rebuild allocates a fresh list and publishes it with
     * a release store; old lists are kept (never freed or rewritten) until
     * rr_destroy, so a pointer the audio thread loaded can never go stale.
     * Rebuilds are rare (create + Rescan), and capped. */
    std::atomic<const rr_folder_list_t *> folders{nullptr};
    std::vector<rr_folder_list_t *> folder_lists;   /* worker-owned history */

    /* ---- worker-only state below ---- */
    std::vector<FolderRoot> roots;
    std::vector<std::string> pool;
    std::vector<uint8_t> pool_role;                /* parallel to pool */
    std::vector<uint32_t> by_role[RR_ROLE_COUNT];  /* pool indices per role */
    std::string pool_dir;
    bool recursive = true;
    bool kit_mode = true;
    float max_len = 2.0f;
    Snapshot cur;
    std::vector<UndoStep> undo;
    uint64_t rng = 0x9E3779B97F4A7C15ull;
};

/* ------------------------------------------------------------------------- */
/* Small helpers (worker thread)                                             */
/* ------------------------------------------------------------------------- */
static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static size_t gcd_size(size_t a, size_t b) {
    while (b) {
        const size_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static uint64_t rng_next(rr_ctx *c) {
    /* xorshift64* */
    uint64_t x = c->rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    c->rng = x;
    return x * 0x2545F4914F6CDD1Dull;
}

static bool ends_with_ci(const char *s, const char *ext) {
    size_t ls = strlen(s), le = strlen(ext);
    return ls >= le && strcasecmp(s + ls - le, ext) == 0;
}

static bool is_audio_file(const char *name) {
    return ends_with_ci(name, ".wav") || ends_with_ci(name, ".aif") || ends_with_ci(name, ".aiff");
}

static bool path_is_under(const std::string &path, const char *root) {
    if (!root || !root[0]) return false;
    size_t n = strlen(root);
    if (path.compare(0, n, root) != 0) return false;
    return path.size() == n || path[n] == '/';
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ------------------------------------------------------------------------- */
/* Kit roles: what a sample is, guessed from its name                        */
/* ------------------------------------------------------------------------- */
/* Names are split into WORDS, never searched as raw text: "808BD_01" is
 * [808, bd, 01] and "KickHard" is [kick, hard], so "bd" matches those but not
 * "ABDUCT". Each pair of neighbouring words is also tried joined, so "Hi Hat"
 * and "HiHat" both reach "hihat". */
enum { kMaxWords = 24, kWordLen = 32 };

/* Fixed buffers, no allocation: this runs for every file in a scan. Words
 * past kMaxWords are ignored and long words are cut to kWordLen - 1 (no role
 * word is longer than 10 letters, so a cut word can't fake a match). */
struct NameWords {
    int n;
    char w[kMaxWords][kWordLen];
};

static void name_words(const char *s, size_t len, NameWords *out) {
    out->n = 0;
    char cur[kWordLen];
    size_t cl = 0;
    auto flush = [&]() {
        if (cl && out->n < kMaxWords) {
            memcpy(out->w[out->n], cur, cl);
            out->w[out->n][cl] = '\0';
            out->n++;
        }
        cl = 0;
    };
    for (size_t i = 0; i < len; i++) {
        const unsigned char ch = (unsigned char)s[i];
        if (!isalnum(ch)) {
            flush();
            continue;
        }
        if (cl) {
            const unsigned char prev = (unsigned char)s[i - 1];
            const bool split = (!!isdigit(ch) != !!isdigit(prev)) ||               /* 808|BD   */
                               (isupper(ch) && islower(prev)) ||                     /* Kick|Hard */
                               (isupper(ch) && isupper(prev) && i + 1 < len &&
                                islower((unsigned char)s[i + 1]));                   /* HH|Open  */
            if (split) flush();
        }
        if (cl < kWordLen - 1) cur[cl++] = (char)tolower(ch);
    }
    flush();
}

struct RoleWord {
    const char *word;
    int role;
};

/* The common names and abbreviations sample packs use. Plurals ("kicks",
 * "hats") are handled by lookup_word, not listed. */
static const RoleWord kRoleWords[] = {
    {"kick", RR_ROLE_KICK}, {"kik", RR_ROLE_KICK}, {"kck", RR_ROLE_KICK}, {"bd", RR_ROLE_KICK},
    {"bassdrum", RR_ROLE_KICK}, {"kickdrum", RR_ROLE_KICK},
    {"snare", RR_ROLE_SNARE}, {"snr", RR_ROLE_SNARE}, {"sn", RR_ROLE_SNARE}, {"sd", RR_ROLE_SNARE},
    {"snaredrum", RR_ROLE_SNARE},
    {"clap", RR_ROLE_CLAP}, {"clp", RR_ROLE_CLAP}, {"cp", RR_ROLE_CLAP}, {"handclap", RR_ROLE_CLAP},
    {"hihat", RR_ROLE_HAT_CLOSED}, {"highhat", RR_ROLE_HAT_CLOSED}, {"hat", RR_ROLE_HAT_CLOSED},
    {"hh", RR_ROLE_HAT_CLOSED}, {"chh", RR_ROLE_HAT_CLOSED}, {"ch", RR_ROLE_HAT_CLOSED},
    {"hhc", RR_ROLE_HAT_CLOSED}, {"closedhat", RR_ROLE_HAT_CLOSED},
    {"ohh", RR_ROLE_HAT_OPEN}, {"oh", RR_ROLE_HAT_OPEN}, {"hho", RR_ROLE_HAT_OPEN},
    {"openhat", RR_ROLE_HAT_OPEN}, {"openhh", RR_ROLE_HAT_OPEN},
    {"crash", RR_ROLE_CRASH}, {"crashes", RR_ROLE_CRASH}, {"crsh", RR_ROLE_CRASH},
    {"cymbal", RR_ROLE_CRASH}, {"cym", RR_ROLE_CRASH}, {"cy", RR_ROLE_CRASH},
    {"splash", RR_ROLE_CRASH}, {"china", RR_ROLE_CRASH},
    {"ride", RR_ROLE_RIDE}, {"rd", RR_ROLE_RIDE},
    {"tom", RR_ROLE_TOM}, {"tm", RR_ROLE_TOM}, {"lt", RR_ROLE_TOM}, {"mt", RR_ROLE_TOM},
    {"ht", RR_ROLE_TOM}, {"ft", RR_ROLE_TOM}, {"floortom", RR_ROLE_TOM}, {"lowtom", RR_ROLE_TOM},
    {"midtom", RR_ROLE_TOM}, {"hitom", RR_ROLE_TOM}, {"hightom", RR_ROLE_TOM},
    {"perc", RR_ROLE_PERC}, {"prc", RR_ROLE_PERC}, {"percussion", RR_ROLE_PERC},
    {"rim", RR_ROLE_PERC}, {"rimshot", RR_ROLE_PERC}, {"rs", RR_ROLE_PERC},
    {"sidestick", RR_ROLE_PERC}, {"stick", RR_ROLE_PERC}, {"cowbell", RR_ROLE_PERC},
    {"cb", RR_ROLE_PERC}, {"conga", RR_ROLE_PERC}, {"bongo", RR_ROLE_PERC},
    {"shaker", RR_ROLE_PERC}, {"shk", RR_ROLE_PERC}, {"tamb", RR_ROLE_PERC},
    {"tambourine", RR_ROLE_PERC}, {"clave", RR_ROLE_PERC}, {"woodblock", RR_ROLE_PERC},
    {"block", RR_ROLE_PERC}, {"snap", RR_ROLE_PERC}, {"fingersnap", RR_ROLE_PERC},
    {"guiro", RR_ROLE_PERC}, {"cabasa", RR_ROLE_PERC}, {"triangle", RR_ROLE_PERC},
    {"agogo", RR_ROLE_PERC}, {"timbale", RR_ROLE_PERC},
};

/* Words that mean "not a drum hit": a loop or phrase, or a voice (so a
 * vocal "Oh Yeah" isn't taken for an open hat, "OH"). */
static const char *const kNotAHitWords[] = {
    "loop", "break", "breakbeat", "fill", "groove", "beat", "bpm", "pattern", "phrase",
    "vocal", "vox", "voice", "chant",
};

static int find_role_word(const char *w) {
    for (const RoleWord &rw : kRoleWords) {
        if (strcmp(w, rw.word) == 0) return rw.role;
    }
    return RR_ROLE_NONE;
}

static bool is_plural(const char *w, size_t n) {
    return n > 3 && w[n - 1] == 's' && w[n - 2] != 's'; /* "kicks", not "bass" */
}

static int lookup_word(const char *w) {
    int r = find_role_word(w);
    const size_t n = strlen(w);
    if (!r && is_plural(w, n)) {
        char single[2 * kWordLen];
        memcpy(single, w, n - 1);
        single[n - 1] = '\0';
        r = find_role_word(single);
    }
    return r;
}

static bool not_a_hit_word(const char *w) {
    const size_t n = strlen(w);
    for (const char *nw : kNotAHitWords) {
        if (strcmp(w, nw) == 0) return true;
        if (is_plural(w, n) && strlen(nw) == n - 1 && strncmp(w, nw, n - 1) == 0) return true;
    }
    return false;
}

/* Role of one name (a file stem or one folder): a role, NONE, or -1 for
 * "not a drum hit", which overrides any role word in it. *saw_open is set if
 * the name says "open" (and *saw_closed for "closed"), so an "open" folder
 * can turn a plain hat into an open hat, unless something says "closed". */
static int role_of_name(const char *s, size_t len, bool *saw_open, bool *saw_closed) {
    NameWords nw;
    name_words(s, len, &nw);
    bool have[RR_ROLE_COUNT] = {false};
    bool open = false;
    for (int i = 0; i < nw.n; i++) {
        const char *w = nw.w[i];
        if (not_a_hit_word(w)) return -1;
        if (strcmp(w, "open") == 0 || strcmp(w, "opn") == 0) open = true;
        if (strcmp(w, "closed") == 0 || strcmp(w, "close") == 0) *saw_closed = true;
        have[lookup_word(w)] = true;
        if (i + 1 < nw.n) {
            char pair[2 * kWordLen];               /* each word < kWordLen: always fits */
            const size_t a = strlen(w), b = strlen(nw.w[i + 1]);
            memcpy(pair, w, a);
            memcpy(pair + a, nw.w[i + 1], b + 1);
            have[lookup_word(pair)] = true;
        }
    }
    if (open) *saw_open = true;
    if (have[RR_ROLE_HAT_CLOSED] && open && !*saw_closed) have[RR_ROLE_HAT_OPEN] = true;
    /* Several role words ("Snare Rim", "Open Hat")? The more specific drum wins. */
    static const int kPriority[] = {RR_ROLE_KICK, RR_ROLE_SNARE, RR_ROLE_CLAP, RR_ROLE_HAT_OPEN,
                                    RR_ROLE_HAT_CLOSED, RR_ROLE_TOM, RR_ROLE_CRASH, RR_ROLE_RIDE,
                                    RR_ROLE_PERC};
    for (int r : kPriority) {
        if (have[r]) return r;
    }
    return RR_ROLE_NONE;
}

/* Kit Mode layout: bottom row kick, kick, snare, clap; second row closed hat,
 * open hat, cymbal, tom. Rows 3-4 stay random. */
static const int kKitTemplate[RR_KIT_SLOTS] = {
    RR_ROLE_KICK, RR_ROLE_KICK, RR_ROLE_SNARE, RR_ROLE_CLAP,
    RR_ROLE_HAT_CLOSED, RR_ROLE_HAT_OPEN, RR_ROLE_CRASH, RR_ROLE_TOM,
};

static bool mkdir_p(const std::string &dir) {
    std::string partial;
    size_t pos = 0;
    while (pos <= dir.size()) {
        size_t next = dir.find('/', pos);
        if (next == std::string::npos) next = dir.size();
        partial = dir.substr(0, next);
        pos = next + 1;
        if (partial.empty()) continue;
        if (mkdir(partial.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    struct stat st;
    return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return false;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        return false;
    }
    const size_t kChunk = 64 * 1024;
    char *buf = (char *)malloc(kChunk);
    bool ok = buf != NULL;
    while (ok) {
        ssize_t n = read(in, buf, kChunk);
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w < 0) {
                if (errno == EINTR) continue;
                ok = false;
                break;
            }
            off += w;
        }
    }
    free(buf);
    close(in);
    if (close(out) != 0) ok = false;
    if (!ok) unlink(dst);
    return ok;
}

static void json_escape_into(std::string &out, const std::string &s) {
    for (char ch : s) {
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += ch;
        } else if ((unsigned char)ch < 0x20) {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)ch);
            out += esc;
        } else {
            out += ch;
        }
    }
}

/* Hand a result to the audio thread. Blocks (worker only) while the ring is full. */
static void push_ready(rr_ctx *c, int pad, rr_buffer_t buf, const std::string &path) {
    rr_ready_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.pad = pad;
    msg.buf = buf;
    snprintf(msg.path, sizeof(msg.path), "%s", path.c_str());
    while (!c->ready.push(msg)) {
        if (c->stop.load(std::memory_order_relaxed)) {
            free(buf.data);
            return;
        }
        void *p;
        while (c->retired.pop(p)) free(p);
        sleep_ms(2);
    }
}

/* ------------------------------------------------------------------------- */
/* Pool scan                                                                 */
/* ------------------------------------------------------------------------- */
static const size_t kPoolCap = 200000;
static const int kMaxDepth = 12;

static void walk(rr_ctx *c, const std::string &dir, int depth) {
    if (c->stop.load(std::memory_order_relaxed) || c->pool.size() >= kPoolCap) return;
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue; /* ., .., hidden, macOS ._ junk */
        std::string full = dir + "/" + e->d_name;
        if (path_is_under(full, c->commit_root)) continue; /* never re-pool committed kits */

        bool is_dir = false, is_reg = false;
        if (e->d_type == DT_DIR) {
            is_dir = true;
        } else if (e->d_type == DT_REG) {
            is_reg = true;
        } else {
            /* DT_UNKNOWN or a symlink: follow only to regular files, never to dirs */
            struct stat st;
            if (lstat(full.c_str(), &st) != 0) continue;
            if (S_ISLNK(st.st_mode)) {
                if (stat(full.c_str(), &st) != 0) continue;
                is_reg = S_ISREG(st.st_mode);
            } else {
                is_dir = S_ISDIR(st.st_mode);
                is_reg = S_ISREG(st.st_mode);
            }
        }

        if (is_dir) {
            if (c->recursive && depth < kMaxDepth) walk(c, full, depth + 1);
        } else if (is_reg && is_audio_file(e->d_name)) {
            c->pool.push_back(full);
            c->pool_role.push_back((uint8_t)rr_classify(full.c_str()));
            if (c->pool.size() >= kPoolCap) break;
        }
    }
    closedir(d);
}

static void scan_pool(rr_ctx *c) {
    c->status.store(RR_ST_SCANNING);
    c->pool.clear();
    c->pool.shrink_to_fit();
    c->pool_role.clear();
    c->pool_role.shrink_to_fit();
    for (std::vector<uint32_t> &v : c->by_role) {
        v.clear();
        v.shrink_to_fit();
    }
    if (!c->pool_dir.empty()) walk(c, c->pool_dir, 0);
    for (size_t i = 0; i < c->pool_role.size(); i++) {
        if (c->pool_role[i]) c->by_role[c->pool_role[i]].push_back((uint32_t)i);
    }
    size_t classified = 0;
    for (int r = 1; r < RR_ROLE_COUNT; r++) {
        c->role_counts[r].store((int)c->by_role[r].size());
        classified += c->by_role[r].size();
    }
    c->role_counts[RR_ROLE_NONE].store((int)(c->pool.size() - classified)); /* no index kept for these */
    c->pool_count.store((int)c->pool.size());
    c->status.store(c->pool.empty() ? RR_ST_NO_POOL : RR_ST_IDLE);
}

/* ------------------------------------------------------------------------- */
/* Folder picker list                                                        */
/* ------------------------------------------------------------------------- */
struct DirInfo {
    std::string path;
    int depth;
    int files;   /* recursive */
};

static const size_t kFolderFileCap = 400000;
static const int kMaxFolderLists = 64;

/* Post-order walk: returns this dir's recursive audio count and records every
 * dir down to RR_FOLDER_DEPTH_MAX. Same skip rules as the pool scan. The walk
 * goes deeper anyway (to count files), so recording more levels costs no I/O. */
static int walk_counts(rr_ctx *c, const std::string &dir, int depth, std::vector<DirInfo> &out, size_t *seen) {
    if (c->stop.load(std::memory_order_relaxed)) return 0;
    DIR *d = opendir(dir.c_str());
    if (!d) return 0;
    size_t my_index = out.size();
    if (depth <= RR_FOLDER_DEPTH_MAX) out.push_back(DirInfo{dir, depth, 0});

    int count = 0;
    std::vector<std::string> subdirs;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        std::string full = dir + "/" + e->d_name;
        if (path_is_under(full, c->commit_root)) continue;
        bool is_dir = false, is_reg = false;
        if (e->d_type == DT_DIR) {
            is_dir = true;
        } else if (e->d_type == DT_REG) {
            is_reg = true;
        } else {
            struct stat st;
            if (lstat(full.c_str(), &st) != 0) continue;
            if (S_ISLNK(st.st_mode)) {
                if (stat(full.c_str(), &st) != 0) continue;
                is_reg = S_ISREG(st.st_mode);   /* never follow dir symlinks */
            } else {
                is_dir = S_ISDIR(st.st_mode);
                is_reg = S_ISREG(st.st_mode);
            }
        }
        if (is_dir) {
            if (depth < kMaxDepth) subdirs.push_back(full);
        } else if (is_reg && is_audio_file(e->d_name)) {
            count++;
            if (++*seen >= kFolderFileCap) break;
        }
    }
    closedir(d);
    for (const std::string &sub : subdirs) {
        if (*seen >= kFolderFileCap) break;
        count += walk_counts(c, sub, depth + 1, out, seen);
    }
    if (depth <= RR_FOLDER_DEPTH_MAX) out[my_index].files = count;
    return count;
}

static void make_label(rr_folder_t *f, const std::string &root_label, const std::string &rel) {
    std::string name = rel.empty() ? root_label : root_label + "/" + rel;
    std::string suffix = " (" + std::to_string(f->files) + ")";
    const size_t max_name = RR_FOLDER_LABEL_MAX - 1 - suffix.size();
    if (name.size() > max_name) {
        /* keep the informative tail: "…kits/808/Kicks" */
        name = "..." + name.substr(name.size() - (max_name - 3));
    }
    snprintf(f->label, sizeof(f->label), "%s%s", name.c_str(), suffix.c_str());
}

static void do_scan_folders(rr_ctx *c) {
    if ((int)c->folder_lists.size() >= kMaxFolderLists) return; /* bounded memory */
    const int prev_status = c->status.load();
    c->status.store(RR_ST_SCANNING);

    rr_folder_list_t *list = (rr_folder_list_t *)calloc(1, sizeof(rr_folder_list_t));
    if (!list) {
        c->status.store(prev_status);
        return;
    }

    struct Pick { int depth; std::string root_label; std::string rel; std::string path; int files; };
    std::vector<Pick> picks;
    size_t seen = 0;
    for (const FolderRoot &r : c->roots) {
        std::vector<DirInfo> dirs;
        walk_counts(c, r.dir, 0, dirs, &seen);
        for (const DirInfo &d : dirs) {
            if (d.files <= 0) continue;
            std::string rel = d.path.size() > r.dir.size() ? d.path.substr(r.dir.size() + 1) : std::string();
            picks.push_back(Pick{d.depth, r.label, rel, d.path, d.files});
        }
    }

    /* Over the 128 limit? Keep shallow folders first (every root, then depth
     * 1, 2, ...), then restore tree order for display. Deeper folders are still
     * reachable with Up One / Browse. Shallow-first also means the plugin's
     * Folder Depth filter shows exactly what a scan to that depth would keep. */
    std::stable_sort(picks.begin(), picks.end(), [](const Pick &a, const Pick &b) { return a.depth < b.depth; });
    if (picks.size() > (size_t)RR_MAX_FOLDERS) picks.resize(RR_MAX_FOLDERS);
    std::stable_sort(picks.begin(), picks.end(), [&](const Pick &a, const Pick &b) {
        size_t ia = 0, ib = 0;
        for (size_t k = 0; k < c->roots.size(); k++) {
            if (c->roots[k].label == a.root_label) ia = k;
            if (c->roots[k].label == b.root_label) ib = k;
        }
        if (ia != ib) return ia < ib;
        return strcasecmp(a.rel.c_str(), b.rel.c_str()) < 0;
    });

    for (const Pick &p : picks) {
        rr_folder_t *f = &list->e[list->count];
        snprintf(f->path, sizeof(f->path), "%s", p.path.c_str());
        f->files = p.files;
        f->depth = p.depth;
        make_label(f, p.root_label, p.rel);
        /* labels are the wire format, so they must be unique */
        for (int k = 0; k < list->count; k++) {
            if (strcmp(list->e[k].label, f->label) == 0) {
                char tag[16];
                snprintf(tag, sizeof(tag), " #%d", list->count + 1);
                size_t room = sizeof(f->label) - strlen(tag) - 1;
                f->label[room] = '\0';
                strncat(f->label, tag, sizeof(f->label) - strlen(f->label) - 1);
                break;
            }
        }
        list->count++;
    }

    c->folder_lists.push_back(list);
    c->folders.store(list, std::memory_order_release);
    c->status.store(prev_status == RR_ST_SCANNING ? (c->pool.empty() ? RR_ST_NO_POOL : RR_ST_IDLE) : prev_status);
}

/* ------------------------------------------------------------------------- */
/* Loading                                                                   */
/* ------------------------------------------------------------------------- */
/* Rough upper bound on file size for max_len seconds of audio: 96 kHz,
 * stereo, 32-bit, plus header slack. Lets us skip obvious stems without
 * decoding them. The decoded length is still checked exactly afterwards. */
static off_t max_bytes_for(float seconds) {
    if (seconds <= 0.0f) return 0;
    return (off_t)(seconds * 96000.0f * 2.0f * 4.0f) + 256 * 1024;
}

static bool load_one(rr_ctx *c, const std::string &path, bool enforce_len, rr_buffer_t *out) {
    memset(out, 0, sizeof(*out));
    if (enforce_len && c->max_len > 0.0f) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return false;
        if (st.st_size > max_bytes_for(c->max_len)) return false;
    }
    char err[256];
    if (!c->loader || c->loader(path.c_str(), out, err, sizeof(err)) != 0) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    if (enforce_len && c->max_len > 0.0f && out->sample_rate > 0 &&
        (float)out->length > c->max_len * (float)out->sample_rate) {
        free(out->data);
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}

/* Record a change: `before` is the kit before it, c->cur the kit after. */
static void push_undo_step(rr_ctx *c, const Snapshot &before) {
    c->undo.push_back(UndoStep{before, c->cur});
    if ((int)c->undo.size() > RR_UNDO_DEPTH) c->undo.erase(c->undo.begin());
    c->undo_depth.store((int)c->undo.size());
}

static void do_roll(rr_ctx *c, uint32_t mask) {
    mask &= (1u << RR_PADS) - 1u;
    if (!mask) return;
    if (c->pool.empty() && !c->pool_dir.empty()) scan_pool(c);
    if (c->pool.empty()) {
        c->status.store(RR_ST_NO_POOL);
        return;
    }
    c->status.store(RR_ST_ROLLING);

    /* Only recorded if something actually lands, so a no-op roll can't push
     * the oldest entry off a full undo stack. */
    const Snapshot before = c->cur;

    const size_t n = c->pool.size();
    bool changed = false;

    /* Already somewhere in the kit: this pad, a pad rolled earlier in this
     * roll, or a locked pad. */
    auto in_kit = [c](const std::string &path) {
        for (const std::string &p : c->cur) {
            if (p == path) return true;
        }
        return false;
    };
    /* Decode and swap in one candidate; false if it doesn't fit (Max Len, bad file). */
    auto try_land = [&](int pad, const std::string &path) {
        rr_buffer_t buf;
        if (!load_one(c, path, true, &buf)) return false;
        c->cur[pad - 1] = path;
        push_ready(c, pad, buf, path);
        changed = true;
        return true;
    };

    for (int pad = 1; pad <= RR_PADS; pad++) {
        if (!(mask & (1u << (pad - 1)))) continue;
        if (c->stop.load(std::memory_order_relaxed)) break;
        bool landed = false;

        /* Kit Mode: try the pad's role, then its fallback (snare <-> clap,
         * ...), fresh files only. Nothing usable falls through to random.
         * The list is walked in a shuffled order (random start, random step
         * coprime to its size) so every file is reachable without allocating:
         * a role with one fresh file left always finds it. */
        const int role = c->kit_mode ? rr_kit_role(pad) : RR_ROLE_NONE;
        const int chain[2] = {role, role ? rr_role_fallback(role) : RR_ROLE_NONE};
        for (int k = 0; k < 2 && !landed; k++) {
            if (!chain[k]) continue;
            const std::vector<uint32_t> &list = c->by_role[chain[k]];
            const size_t m = list.size();
            if (!m) continue;
            size_t step = 1;
            for (int t = 0; t < 8 && m > 2; t++) {
                const size_t s = 1 + (size_t)(rng_next(c) % (m - 1));
                if (gcd_size(s, m) == 1) {
                    step = s;
                    break;
                }
            }
            const int kRoleLoads = 16;   /* decode attempts, not candidates looked at */
            int loads = 0;
            size_t at = (size_t)(rng_next(c) % m);
            for (size_t j = 0; j < m && loads < kRoleLoads && !landed; j++, at = (at + step) % m) {
                const std::string &path = c->pool[list[at]];
                if (in_kit(path)) continue;
                loads++;
                landed = try_land(pad, path);
            }
        }

        const int kAttempts = 24;
        for (int attempt = 0; attempt < kAttempts && !landed; attempt++) {
            const std::string &path = c->pool[(size_t)(rng_next(c) % n)];
            /* Prefer a file that isn't in the kit yet; relax that once a small
             * pool makes it impossible. */
            if (attempt < kAttempts / 2 && n > 1 && in_kit(path)) continue;
            landed = try_land(pad, path);
        }
    }

    if (changed) push_undo_step(c, before);
    c->status.store(RR_ST_IDLE);
}

static void do_load(rr_ctx *c, int pad, const std::string &path, bool record_undo) {
    if (pad < 1 || pad > RR_PADS) return;
    const Snapshot before = c->cur;
    c->cur[pad - 1] = path;
    if (record_undo && before[pad - 1] != path) push_undo_step(c, before);
    rr_buffer_t buf;
    memset(&buf, 0, sizeof(buf));
    if (!path.empty()) {
        /* Explicit picks ignore max_len: you chose it, you get it. A failed
         * decode clears the pad (MrDrums behaviour) but keeps the path. */
        load_one(c, path, false, &buf);
    }
    push_ready(c, pad, buf, path);
}

/* Revert the last step on the pads it changed, except pads that are locked
 * now or that you've replaced since (they no longer hold step.after). The
 * step is consumed even if every pad is skipped, so Undo can't get stuck. */
static void do_undo(rr_ctx *c, uint32_t locked) {
    if (c->undo.empty()) return;
    c->status.store(RR_ST_ROLLING);
    const UndoStep step = c->undo.back();
    c->undo.pop_back();
    c->undo_depth.store((int)c->undo.size());
    for (int pad = 1; pad <= RR_PADS; pad++) {
        const int i = pad - 1;
        if (locked & (1u << i)) continue;
        if (step.before[i] == step.after[i]) continue;   /* the step didn't touch it */
        if (c->cur[i] != step.after[i]) continue;        /* changed since: keep it */
        do_load(c, pad, step.before[i], false);
    }
    c->status.store(RR_ST_IDLE);
}

/* Returns true if the result went to the audio thread, which then settles
 * `pending` when it polls it (so busy stays up until the result is applied). */
static bool do_job(rr_ctx *c, const Cmd &cmd) {
    if (!cmd.job) return false;
    void *result = cmd.job(cmd.path);
    if (!result) return false;
    while (!c->jobs_done.push(result)) {
        if (c->stop.load(std::memory_order_relaxed)) {
            free(result);
            return false;
        }
        void *p;
        while (c->retired.pop(p)) free(p);
        sleep_ms(2);
    }
    return true;
}

static void do_commit(rr_ctx *c) {
    bool any = false;
    for (const std::string &p : c->cur) any = any || !p.empty();
    if (!any) {
        c->status.store(RR_ST_ERROR);
        return;
    }
    c->status.store(RR_ST_COMMITTING);

    char stamp[64];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(stamp, sizeof(stamp), "Fluke %Y-%m-%d %H%M%S", &tmv);

    std::string dest = std::string(c->commit_root) + "/" + stamp;
    if (!mkdir_p(dest)) {
        c->status.store(RR_ST_ERROR);
        return;
    }

    bool ok = true;
    std::string manifest = "{\n  \"tool\": \"flukedrums\",\n  \"pads\": [";
    bool first = true;
    for (int pad = 1; pad <= RR_PADS; pad++) {
        const std::string &src = c->cur[pad - 1];
        if (src.empty()) continue;
        char fname[RR_PATH_MAX];
        snprintf(fname, sizeof(fname), "%02d %s", pad, base_name(src.c_str()));
        std::string dst = dest + "/" + fname;
        if (!copy_file(src.c_str(), dst.c_str())) ok = false;

        manifest += first ? "\n    " : ",\n    ";
        first = false;
        manifest += "{\"pad\": " + std::to_string(pad) + ", \"file\": \"";
        json_escape_into(manifest, fname);
        manifest += "\", \"kind\": \"";
        json_escape_into(manifest, rr_role_name(rr_classify(src.c_str())));
        manifest += "\", \"source\": \"";
        json_escape_into(manifest, src);
        manifest += "\"}";
    }
    manifest += "\n  ]\n}\n";

    std::string mpath = dest + "/flukedrums-kit.json";
    FILE *mf = fopen(mpath.c_str(), "w");
    if (mf) {
        fwrite(manifest.data(), 1, manifest.size(), mf);
        if (fclose(mf) != 0) ok = false;
    } else {
        ok = false;
    }

    if (ok) {
        int idx = c->commit_name_idx.load(std::memory_order_relaxed);
        int next = (idx == 0) ? 1 : 0;
        snprintf(c->commit_names[next], RR_PATH_MAX, "%s", stamp);
        c->commit_name_idx.store(next, std::memory_order_release);
    }
    c->status.store(ok ? RR_ST_COMMITTED : RR_ST_ERROR);
}

/* ------------------------------------------------------------------------- */
/* Worker                                                                    */
/* ------------------------------------------------------------------------- */
static void demote_self(void) {
    /* Threads spawned from the SPI callback inherit SCHED_FIFO 90. Drop that
     * FIRST, then get off core 3 (the audio core). See Schwung MODULES.md. */
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#endif
}

/* Returns true if the command's pending count is now owned by the audio thread. */
static bool handle(rr_ctx *c, const Cmd &cmd) {
    switch (cmd.type) {
    case CMD_SET_POOL:
        /* Spinning the Folder knob queues a pool change per detent: only
         * the last one queued is worth scanning. */
        if (c->cmds.any_queued([](const Cmd &q) { return q.type == CMD_SET_POOL; })) break;
        c->pool_dir = cmd.path;
        while (c->pool_dir.size() > 1 && c->pool_dir.back() == '/') c->pool_dir.pop_back();
        c->recursive = cmd.flag != 0;
        scan_pool(c);
        break;
    case CMD_MAX_LEN:
        c->max_len = cmd.f;
        break;
    case CMD_KIT_MODE:
        c->kit_mode = cmd.flag != 0;
        break;
    case CMD_ROLL:
        do_roll(c, cmd.mask);
        break;
    case CMD_LOAD: {
        /* Browser live-preview fires a load per cursor step: skip any load that a
         * later queued load for the same pad already supersedes. */
        const int pad = cmd.pad;
        bool superseded = c->cmds.any_queued([pad](const Cmd &q) {
            return q.type == CMD_LOAD && q.pad == pad;
        });
        if (!superseded) do_load(c, cmd.pad, cmd.path, cmd.flag != 0);
        break;
    }
    case CMD_UNDO:
        do_undo(c, cmd.mask);
        break;
    case CMD_COMMIT:
        do_commit(c);
        break;
    case CMD_FOLDER_ROOT: {
        std::string dir = cmd.path;
        while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
        bool dup = false;
        for (const FolderRoot &r : c->roots) dup = dup || r.dir == dir;
        if (!dup && !dir.empty() && (int)c->roots.size() < RR_MAX_ROOTS) {
            c->roots.push_back(FolderRoot{dir, cmd.label});
        }
        break;
    }
    case CMD_SCAN_FOLDERS:
        if (c->cmds.any_queued([](const Cmd &q) { return q.type == CMD_SCAN_FOLDERS; })) break;
        do_scan_folders(c);
        break;
    case CMD_JOB: {
        const rr_job_fn fn = cmd.job;
        if (c->cmds.any_queued([fn](const Cmd &q) { return q.type == CMD_JOB && q.job == fn; })) break;
        return do_job(c, cmd);
    }
    default:
        break;
    }
    return false;
}

static void *worker_main(void *arg) {
    rr_ctx *c = (rr_ctx *)arg;
    demote_self();

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    c->rng ^= (uint64_t)ts.tv_nsec * 0x9E3779B97F4A7C15ull ^ (uint64_t)ts.tv_sec ^ (uint64_t)(uintptr_t)c;
    if (!c->rng) c->rng = 1;

    while (!c->stop.load(std::memory_order_acquire)) {
        bool did = false;
        void *p;
        while (c->retired.pop(p)) {
            free(p);
            did = true;
        }
        Cmd cmd;
        if (c->cmds.pop(cmd)) {
            if (!handle(c, cmd)) c->pending.fetch_sub(1, std::memory_order_acq_rel);
            did = true;
        }
        if (!did) sleep_ms(3);
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */
extern "C" {

rr_ctx_t *rr_create(rr_loader_fn loader, const char *commit_root) {
    rr_ctx *c = new (std::nothrow) rr_ctx();
    if (!c) return NULL;
    c->loader = loader;
    snprintf(c->commit_root, sizeof(c->commit_root), "%s", commit_root ? commit_root : "");
    c->thread_ok = pthread_create(&c->thread, NULL, worker_main, c) == 0;
    return c;
}

void rr_destroy(rr_ctx_t *c) {
    if (!c) return;
    c->stop.store(true, std::memory_order_release);
    if (c->thread_ok) pthread_join(c->thread, NULL);

    /* Worker is gone: this thread now owns both ends of every ring. */
    rr_ready_t r;
    while (c->ready.pop(r)) free(r.buf.data);
    void *p;
    while (c->retired.pop(p)) free(p);
    while (c->jobs_done.pop(p)) free(p);
    for (rr_folder_list_t *l : c->folder_lists) free(l);
    delete c;
}

static int enqueue(rr_ctx_t *c, const Cmd &cmd) {
    if (!c || !c->thread_ok) return -1;
    c->pending.fetch_add(1, std::memory_order_acq_rel);
    if (!c->cmds.push(cmd)) {
        c->pending.fetch_sub(1, std::memory_order_acq_rel);
        return -1;
    }
    return 0;
}

static Cmd make_cmd(int type) {
    Cmd cmd;
    cmd.type = type;
    cmd.pad = 0;
    cmd.mask = 0;
    cmd.flag = 0;
    cmd.f = 0.0f;
    cmd.job = nullptr;
    cmd.path[0] = '\0';
    cmd.label[0] = '\0';
    return cmd;
}

int rr_add_folder_root(rr_ctx_t *c, const char *dir, const char *label) {
    Cmd cmd = make_cmd(CMD_FOLDER_ROOT);
    snprintf(cmd.path, sizeof(cmd.path), "%s", dir ? dir : "");
    snprintf(cmd.label, sizeof(cmd.label), "%s", label && label[0] ? label : "Samples");
    return enqueue(c, cmd);
}

int rr_scan_folders(rr_ctx_t *c) { return enqueue(c, make_cmd(CMD_SCAN_FOLDERS)); }

const rr_folder_list_t *rr_folders(const rr_ctx_t *c) {
    return c ? c->folders.load(std::memory_order_acquire) : nullptr;
}

int rr_set_pool(rr_ctx_t *c, const char *dir, int recursive) {
    Cmd cmd = make_cmd(CMD_SET_POOL);
    snprintf(cmd.path, sizeof(cmd.path), "%s", dir ? dir : "");
    cmd.flag = recursive ? 1 : 0;
    return enqueue(c, cmd);
}

int rr_set_max_len(rr_ctx_t *c, float seconds) {
    Cmd cmd = make_cmd(CMD_MAX_LEN);
    cmd.f = seconds;
    return enqueue(c, cmd);
}

int rr_set_kit_mode(rr_ctx_t *c, int on) {
    Cmd cmd = make_cmd(CMD_KIT_MODE);
    cmd.flag = on ? 1 : 0;
    return enqueue(c, cmd);
}

int rr_role_count(const rr_ctx_t *c, int role) {
    if (!c || role < 0 || role >= RR_ROLE_COUNT) return 0;
    return c->role_counts[role].load(std::memory_order_relaxed);
}

int rr_kit_role(int pad) {
    return (pad >= 1 && pad <= RR_KIT_SLOTS) ? kKitTemplate[pad - 1] : RR_ROLE_NONE;
}

int rr_role_fallback(int role) {
    switch (role) {
    case RR_ROLE_SNARE: return RR_ROLE_CLAP;
    case RR_ROLE_CLAP: return RR_ROLE_SNARE;
    case RR_ROLE_HAT_CLOSED: return RR_ROLE_HAT_OPEN;
    case RR_ROLE_HAT_OPEN: return RR_ROLE_HAT_CLOSED;
    case RR_ROLE_CRASH: return RR_ROLE_RIDE;
    case RR_ROLE_RIDE: return RR_ROLE_CRASH;
    case RR_ROLE_TOM: return RR_ROLE_PERC;
    default: return RR_ROLE_NONE;
    }
}

const char *rr_role_name(int role) {
    switch (role) {
    case RR_ROLE_KICK: return "kick";
    case RR_ROLE_SNARE: return "snare";
    case RR_ROLE_CLAP: return "clap";
    case RR_ROLE_HAT_CLOSED: return "closed hat";
    case RR_ROLE_HAT_OPEN: return "open hat";
    case RR_ROLE_CRASH: return "crash";
    case RR_ROLE_RIDE: return "ride";
    case RR_ROLE_TOM: return "tom";
    case RR_ROLE_PERC: return "perc";
    default: return "other";
    }
}

/* The file's own name decides first. If it says nothing, its folder does,
 * then the folder above ("Kicks/Acoustic/wood.wav" is a kick). A loop-ish
 * name in the file or its folder means "not a hit", whatever else it says. */
int rr_classify(const char *path) {
    if (!path || !path[0]) return RR_ROLE_NONE;
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    const char *dot = strrchr(name, '.');
    bool open = false, closed = false;
    int r = role_of_name(name, dot ? (size_t)(dot - name) : strlen(name), &open, &closed);
    if (r < 0) return RR_ROLE_NONE;

    const char *seg_end = slash;
    for (int level = 0; level < 2 && seg_end && seg_end > path; level++) {
        const char *seg = seg_end;
        while (seg > path && seg[-1] != '/') seg--;
        const int fr = role_of_name(seg, (size_t)(seg_end - seg), &open, &closed);
        if (fr < 0) {
            if (level == 0) return RR_ROLE_NONE;            /* a loop folder vetoes */
            break;
        }
        if (r) break;                                      /* the name already said */
        r = fr;
        if (r || seg == path) break;
        seg_end = seg - 1;
    }
    if (r == RR_ROLE_HAT_CLOSED && open && !closed) r = RR_ROLE_HAT_OPEN; /* "Hats/Open/1.wav" */
    return r;
}

int rr_roll(rr_ctx_t *c, uint32_t pad_mask) {
    Cmd cmd = make_cmd(CMD_ROLL);
    cmd.mask = pad_mask;
    return enqueue(c, cmd);
}

int rr_load(rr_ctx_t *c, int pad, const char *path, int record_undo) {
    if (pad < 1 || pad > RR_PADS) return -1;
    Cmd cmd = make_cmd(CMD_LOAD);
    cmd.pad = pad;
    cmd.flag = record_undo ? 1 : 0;
    snprintf(cmd.path, sizeof(cmd.path), "%s", path ? path : "");
    return enqueue(c, cmd);
}

int rr_undo(rr_ctx_t *c, uint32_t locked_mask) {
    Cmd cmd = make_cmd(CMD_UNDO);
    cmd.mask = locked_mask;
    return enqueue(c, cmd);
}

int rr_job(rr_ctx_t *c, rr_job_fn fn, const char *path) {
    if (!fn) return -1;
    Cmd cmd = make_cmd(CMD_JOB);
    cmd.job = fn;
    snprintf(cmd.path, sizeof(cmd.path), "%s", path ? path : "");
    return enqueue(c, cmd);
}

int rr_poll_job(rr_ctx_t *c, void **out) {
    if (!c || !out) return 0;
    if (!c->jobs_done.pop(*out)) return 0;
    c->pending.fetch_sub(1, std::memory_order_acq_rel);   /* taken over from the worker */
    return 1;
}

int rr_free_later(rr_ctx_t *c, void *p) {
    if (!p) return 0;
    if (!c) return -1;
    return c->retired.push(p) ? 0 : -1;
}

int rr_commit(rr_ctx_t *c) { return enqueue(c, make_cmd(CMD_COMMIT)); }

int rr_poll(rr_ctx_t *c, rr_ready_t *out) {
    if (!c || !out) return 0;
    return c->ready.pop(*out) ? 1 : 0;
}

int rr_retire(rr_ctx_t *c, float *data) {
    if (!data) return 0;
    if (!c) return -1;
    return c->retired.push((void *)data) ? 0 : -1;
}

rr_status_t rr_status(const rr_ctx_t *c) {
    return c ? (rr_status_t)c->status.load(std::memory_order_relaxed) : RR_ST_ERROR;
}

int rr_pool_count(const rr_ctx_t *c) { return c ? c->pool_count.load(std::memory_order_relaxed) : 0; }

int rr_undo_depth(const rr_ctx_t *c) { return c ? c->undo_depth.load(std::memory_order_relaxed) : 0; }

int rr_busy(const rr_ctx_t *c) { return c ? c->pending.load(std::memory_order_acquire) > 0 : 0; }

int rr_last_commit_name(const rr_ctx_t *c, char *buf, int buf_len) {
    if (!c || !buf || buf_len <= 0) return 0;
    int idx = c->commit_name_idx.load(std::memory_order_acquire);
    if (idx < 0) {
        buf[0] = '\0';
        return 0;
    }
    int n = snprintf(buf, (size_t)buf_len, "%s", c->commit_names[idx]);
    return n < 0 ? 0 : n;
}

const char *rr_status_label(rr_status_t st) {
    switch (st) {
    case RR_ST_IDLE: return "Ready";
    case RR_ST_NO_POOL: return "No samples";
    case RR_ST_SCANNING: return "Scanning";
    case RR_ST_ROLLING: return "Rolling";
    case RR_ST_COMMITTING: return "Saving";
    case RR_ST_COMMITTED: return "Saved";
    case RR_ST_ERROR: return "Error";
    }
    return "?";
}

} /* extern "C" */
