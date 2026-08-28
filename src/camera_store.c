#include "camera_store.h"
#include <stdlib.h>
#include <string.h>

void camera_store_init(camera_store_t *s) {
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->lock, NULL);
}

void camera_store_destroy(camera_store_t *s) {
    free(s->rgb);
    pthread_mutex_destroy(&s->lock);
}

void camera_store_set_frame(camera_store_t *s, const unsigned char *rgb, int w, int h) {
    size_t bytes = (size_t)w * (size_t)h * 3;
    pthread_mutex_lock(&s->lock);
    unsigned char *buf = realloc(s->rgb, bytes);
    if (buf) {
        memcpy(buf, rgb, bytes);
        s->rgb = buf;
        s->w = w;
        s->h = h;
        s->version++;
    }
    pthread_mutex_unlock(&s->lock);
}

void camera_store_set_fetch_status(camera_store_t *s, int ok) {
    pthread_mutex_lock(&s->lock);
    s->last_fetch_ok = ok;
    pthread_mutex_unlock(&s->lock);
}

int camera_store_snapshot_if_new(camera_store_t *s, unsigned long *inout_last_version, unsigned char *dst,
                                  int *out_w, int *out_h) {
    int changed = 0;
    pthread_mutex_lock(&s->lock);
    if (s->rgb && s->version != *inout_last_version) {
        size_t bytes = (size_t)s->w * (size_t)s->h * 3;
        memcpy(dst, s->rgb, bytes);
        *out_w = s->w;
        *out_h = s->h;
        *inout_last_version = s->version;
        changed = 1;
    }
    pthread_mutex_unlock(&s->lock);
    return changed;
}

int camera_store_last_fetch_ok(camera_store_t *s) {
    pthread_mutex_lock(&s->lock);
    int ok = s->last_fetch_ok;
    pthread_mutex_unlock(&s->lock);
    return ok;
}
