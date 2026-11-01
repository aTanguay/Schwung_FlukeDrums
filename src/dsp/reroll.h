/*
 * reroll.h — async "random kit" engine for a 16-pad sampler.
 *
 * Everything that touches the disk (scanning the pool, decoding samples,
 * copying files on commit, free()) happens on ONE worker thread that demotes
 * itself to SCHED_OTHER off the audio core. The audio thread only ever talks
 * to it through three preallocated single-producer/single-consumer rings:
 *
 *   commands  audio  -> worker   roll / load / undo / commit / settings
 *   ready     worker -> audio    a decoded sample for a pad (ownership moves)
 *   retired   audio  -> worker   a buffer no voice references any more
 *
 * Every rr_* call marked AUDIO-SAFE does no allocation, no I/O, no locking and
 * no syscalls. Safe to call from set_param / get_param / render_block.
 *
 * MIT — part of the Reroll module (fork of MrDrums by handcraftedcc).
 */
#ifndef REROLL_H
#define REROLL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RR_PADS = 16,
    RR_PATH_MAX = 512,
    RR_UNDO_DEPTH = 32,
    /* Folder picker. 128 is Schwung's enum option limit (MAX_ENUM_OPTIONS). */
    RR_MAX_FOLDERS = 128,
    RR_MAX_ROOTS = 4,
    RR_FOLDER_LABEL_MAX = 72,
    RR_FOLDER_DEPTH = 3       /* list folders up to this many levels below a root */
};

/* One pickable pool folder. files counts audio in it AND all subfolders. */
typedef struct {
    char label[RR_FOLDER_LABEL_MAX];  /* "My/Drums/Kicks (412)" — unique within a list */
    char path[RR_PATH_MAX];
    int files;
} rr_folder_t;

/* Published by the worker as an immutable snapshot: once rr_folders() hands
 * you a pointer, it stays valid and unchanged until rr_destroy(). */
typedef struct {
    int count;
    rr_folder_t e[RR_MAX_FOLDERS];
} rr_folder_list_t;

/* Same layout as MrDrums' sample_buffer_t: mono float, owned by whoever holds it. */
typedef struct {
    float *data;
    int length;
    int sample_rate;
} rr_buffer_t;

/* Decoder supplied by the host plugin. Runs on the worker thread only.
 * Must be reentrant. Returns 0 on success and fills *out (heap data). */
typedef int (*rr_loader_fn)(const char *path, rr_buffer_t *out, char *err, int err_len);

/* A decoded sample ready to swap in. pad is 1-based. buf.data == NULL means
 * "clear this pad". After rr_poll() returns it, the caller owns buf.data. */
typedef struct {
    int pad;
    rr_buffer_t buf;
    char path[RR_PATH_MAX];
} rr_ready_t;

typedef enum {
    RR_ST_IDLE = 0,
    RR_ST_NO_POOL,      /* pool dir unset, missing, or holds no audio files */
    RR_ST_SCANNING,
    RR_ST_ROLLING,
    RR_ST_COMMITTING,
    RR_ST_COMMITTED,    /* last commit succeeded; see rr_last_commit_name() */
    RR_ST_ERROR         /* last commit failed */
} rr_status_t;

typedef struct rr_ctx rr_ctx_t;

/* Internal to this module's dsp.so: keep generic rr_* names out of the
 * dynamic symbol table so they can't collide with another module's. */
#if defined(__GNUC__)
#define RR_API __attribute__((visibility("hidden")))
#else
#define RR_API
#endif

/* Create/destroy. NOT audio-safe (allocate, spawn/join a thread) — call once
 * from create_instance / destroy_instance. destroy frees every buffer still in
 * flight (ready ring, retired ring). */
RR_API rr_ctx_t *rr_create(rr_loader_fn loader, const char *commit_root);
RR_API void rr_destroy(rr_ctx_t *ctx);

/* ---- AUDIO-SAFE: queue commands. Return 0 if queued, -1 if the ring is full. */
RR_API int rr_set_pool(rr_ctx_t *ctx, const char *dir, int recursive);  /* (re)scans */
RR_API int rr_set_max_len(rr_ctx_t *ctx, float seconds);                /* <=0: no limit */
RR_API int rr_roll(rr_ctx_t *ctx, uint32_t pad_mask);                   /* bit 0 = pad 1 */
RR_API int rr_load(rr_ctx_t *ctx, int pad, const char *path, int record_undo); /* "" clears */
RR_API int rr_undo(rr_ctx_t *ctx);
RR_API int rr_commit(rr_ctx_t *ctx);
/* Folder picker: register library roots ("My" -> UserLibrary/Samples, ...),
 * then (re)build the list. Roots that don't exist are skipped. */
RR_API int rr_add_folder_root(rr_ctx_t *ctx, const char *dir, const char *label);
RR_API int rr_scan_folders(rr_ctx_t *ctx);

/* ---- AUDIO-SAFE: results. */
RR_API int rr_poll(rr_ctx_t *ctx, rr_ready_t *out);        /* 1 = got one, 0 = empty */
RR_API int rr_retire(rr_ctx_t *ctx, float *data);          /* 0 ok, -1 full (try again later) */

/* ---- AUDIO-SAFE: readouts. */
RR_API rr_status_t rr_status(const rr_ctx_t *ctx);
RR_API int rr_pool_count(const rr_ctx_t *ctx);
RR_API int rr_undo_depth(const rr_ctx_t *ctx);
RR_API int rr_busy(const rr_ctx_t *ctx);                   /* commands queued or in progress */
/* Copies the folder name of the last successful commit. Returns length, or 0. */
RR_API int rr_last_commit_name(const rr_ctx_t *ctx, char *buf, int buf_len);
/* Latest folder list, or NULL before the first scan finishes. */
RR_API const rr_folder_list_t *rr_folders(const rr_ctx_t *ctx);

RR_API const char *rr_status_label(rr_status_t st);

#ifdef __cplusplus
}
#endif

#endif
