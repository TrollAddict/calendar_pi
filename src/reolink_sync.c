#define _DEFAULT_SOURCE
#include "reolink_sync.h"
#include "config_store.h"
#include "reolink_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Kept well above a couple of seconds: this project's camera has no way
 * to configure a lower main-stream resolution, so every poll decodes a
 * full ~5MP JPEG (see camera_store.h's CAMERA_MAX_W/H) -- fine on a Pi
 * Zero W's single ARM11 core as an occasional cost, not as something to
 * repeat every couple of seconds. */
#define POLL_INTERVAL_SEC 30
#define CONFIG_MISSING_POLL_SEC 5 /* how often to check whether camera.conf has been dropped in place */

/* After a few consecutive failures, back off hard rather than continuing
 * to poll every POLL_INTERVAL_SEC. This matters more here than it would
 * for a plain connectivity blip: Reolink cameras have an anti-brute-force
 * login lockout (a handful of failed attempts locks logins out for
 * several minutes), so hammering a failing login doesn't just waste
 * effort -- it actively re-triggers/extends that lockout. */
#define FAILURE_BACKOFF_SEC 300
#define FAILURE_BACKOFF_STREAK 2

static void set_status(reolink_sync_t *sync, reolink_sync_status_t status) {
    pthread_mutex_lock(&sync->lock);
    sync->status = status;
    pthread_mutex_unlock(&sync->lock);
}

/* Waits up to seconds for a stop signal. Returns 1 if stop was
 * requested (caller should exit promptly), 0 if the wait elapsed. */
static int interruptible_wait(reolink_sync_t *sync, int seconds) {
    pthread_mutex_lock(&sync->lock);
    if (!sync->stop) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += seconds;
        while (!sync->stop) {
            int rc = pthread_cond_timedwait(&sync->wake, &sync->lock, &deadline);
            if (rc != 0) break; /* timed out */
        }
    }
    int stop = sync->stop;
    pthread_mutex_unlock(&sync->lock);
    return stop;
}

static void *sync_thread_main(void *arg) {
    reolink_sync_t *sync = (reolink_sync_t *)arg;

    camera_config_t cfg;
    while (config_load_camera(&cfg) != 0) {
        set_status(sync, REOLINK_STATE_CONFIG_MISSING);
        if (interruptible_wait(sync, CONFIG_MISSING_POLL_SEC)) return NULL;
    }

    int consecutive_failures = 0;
    while (!sync->stop) {
        unsigned char *rgb = NULL;
        int w = 0, h = 0;
        int ok = reolink_fetch_snapshot(&cfg, &rgb, &w, &h) == 0;

        if (ok) {
            camera_store_set_frame(sync->store, rgb, w, h);
            free(rgb);
        } else {
            fprintf(stderr, "reolink_sync: snapshot fetch failed\n");
        }

        camera_store_set_fetch_status(sync->store, ok);
        set_status(sync, ok ? REOLINK_STATE_CONNECTED : REOLINK_STATE_OFFLINE);
        consecutive_failures = ok ? 0 : consecutive_failures + 1;

        int sleep_sec = (!ok && consecutive_failures > FAILURE_BACKOFF_STREAK) ? FAILURE_BACKOFF_SEC
                                                                                : POLL_INTERVAL_SEC;
        if (interruptible_wait(sync, sleep_sec)) return NULL;
    }
    return NULL;
}

int reolink_sync_start(reolink_sync_t *sync, camera_store_t *store) {
    memset(sync, 0, sizeof(*sync));
    sync->store = store;
    sync->status = REOLINK_STATE_CONFIG_MISSING;
    pthread_mutex_init(&sync->lock, NULL);
    pthread_cond_init(&sync->wake, NULL);

    if (pthread_create(&sync->thread, NULL, sync_thread_main, sync) != 0) return -1;
    sync->thread_started = 1;
    return 0;
}

void reolink_sync_stop(reolink_sync_t *sync) {
    pthread_mutex_lock(&sync->lock);
    sync->stop = 1;
    pthread_cond_broadcast(&sync->wake);
    pthread_mutex_unlock(&sync->lock);

    if (sync->thread_started) pthread_join(sync->thread, NULL);

    pthread_cond_destroy(&sync->wake);
    pthread_mutex_destroy(&sync->lock);
}

reolink_sync_status_t reolink_sync_get_status(reolink_sync_t *sync) {
    pthread_mutex_lock(&sync->lock);
    reolink_sync_status_t status = sync->status;
    pthread_mutex_unlock(&sync->lock);
    return status;
}
