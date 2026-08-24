#ifndef CAMERA_STORE_H
#define CAMERA_STORE_H

#include <pthread.h>

/* Defensive cap on decoded frame size -- see reolink_client.c. Keeps the
 * scratch buffer camera_view.c allocates once bounded (~6MB at this cap),
 * comfortably inside a Pi Zero W's 512MB RAM. */
#define CAMERA_MAX_W 1920
#define CAMERA_MAX_H 1080

typedef struct {
    pthread_mutex_t lock;
    unsigned char *rgb; /* malloc'd w*h*3 (RGB888), NULL until the first frame */
    int w, h;
    unsigned long version; /* bumped on every new frame */
    int last_fetch_ok;
} camera_store_t;

void camera_store_init(camera_store_t *s);
void camera_store_destroy(camera_store_t *s);

/* Copies rgb (w*h*3 bytes) into the store, growing its internal buffer
 * as needed, and bumps version. Caller retains ownership of rgb. */
void camera_store_set_frame(camera_store_t *s, const unsigned char *rgb, int w, int h);

void camera_store_set_fetch_status(camera_store_t *s, int ok);

/* If the store's version has advanced past *inout_last_version, copies
 * the current frame into dst (which must be at least
 * CAMERA_MAX_W*CAMERA_MAX_H*3 bytes) and updates *inout_last_version,
 * *out_w, *out_h, returning 1. Otherwise returns 0 and touches nothing
 * else (caller keeps using whatever it already has, e.g. its existing
 * GL texture). */
int camera_store_snapshot_if_new(camera_store_t *s, unsigned long *inout_last_version, unsigned char *dst,
                                  int *out_w, int *out_h);

int camera_store_last_fetch_ok(camera_store_t *s);

#endif
