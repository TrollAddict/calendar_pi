#ifndef REOLINK_SYNC_H
#define REOLINK_SYNC_H

#include "camera_store.h"
#include <pthread.h>
#include <time.h>

typedef enum {
    REOLINK_STATE_CONFIG_MISSING, /* no camera.conf -- see docs/REOLINK_SETUP.md */
    REOLINK_STATE_OFFLINE,        /* fetch/decode failing; showing the last good frame, if any */
    REOLINK_STATE_CONNECTED,      /* healthy, at least one successful snapshot */
} reolink_sync_status_t;

typedef struct {
    pthread_t thread;
    int thread_started;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    volatile int stop;

    camera_store_t *store;

    reolink_sync_status_t status;
} reolink_sync_t;

/* Starts the background snapshot-polling thread. Returns 0 on success. */
int reolink_sync_start(reolink_sync_t *sync, camera_store_t *store);

/* Signals the thread to stop and joins it. */
void reolink_sync_stop(reolink_sync_t *sync);

reolink_sync_status_t reolink_sync_get_status(reolink_sync_t *sync);

#endif
