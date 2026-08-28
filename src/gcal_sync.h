#ifndef GCAL_SYNC_H
#define GCAL_SYNC_H

#include "event_store.h"
#include <pthread.h>
#include <time.h>

typedef enum {
    GCAL_STATE_CONFIG_MISSING, /* no client.conf -- see docs/GOOGLE_CALENDAR_SETUP.md */
    GCAL_STATE_NEEDS_AUTH,     /* no token file yet -- waiting for tools/authorize_gcal.py's output */
    GCAL_STATE_SYNCING,        /* healthy, at least one successful fetch */
    GCAL_STATE_OFFLINE,        /* fetch/refresh failing; showing stale cached events */
} gcal_sync_status_t;

typedef struct {
    pthread_t thread;
    int thread_started;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    volatile int stop;

    event_store_t *store;

    gcal_sync_status_t status;
} gcal_sync_t;

/* Starts the background sync thread. Returns 0 on success. */
int gcal_sync_start(gcal_sync_t *sync, event_store_t *store);

/* Signals the thread to stop and joins it (bounded by the interruptible
 * sleeps in the thread body, not by the sync interval). */
void gcal_sync_stop(gcal_sync_t *sync);

/* Snapshot of current status for rendering. */
gcal_sync_status_t gcal_sync_get_status(gcal_sync_t *sync);

#endif
