#ifndef GTASKS_SYNC_H
#define GTASKS_SYNC_H

#include "task_store.h"
#include <pthread.h>
#include <time.h>

typedef enum {
    GTASKS_STATE_CONFIG_MISSING, /* no client.conf -- see docs/GOOGLE_CALENDAR_SETUP.md */
    GTASKS_STATE_NEEDS_AUTH,     /* no token file yet -- waiting for tools/authorize_gcal.py's output */
    GTASKS_STATE_SYNCING,        /* healthy, at least one successful fetch */
    GTASKS_STATE_OFFLINE,        /* fetch/refresh failing (network, or a token not yet re-authorized
                                   * for the tasks scope); showing stale cached tasks */
} gtasks_sync_status_t;

typedef struct {
    pthread_t thread;
    int thread_started;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    volatile int stop;

    task_store_t *store;

    gtasks_sync_status_t status;
} gtasks_sync_t;

/* Starts the background sync thread. Returns 0 on success. */
int gtasks_sync_start(gtasks_sync_t *sync, task_store_t *store);

/* Signals the thread to stop and joins it (bounded by the interruptible
 * sleeps in the thread body, not by the sync interval). */
void gtasks_sync_stop(gtasks_sync_t *sync);

/* Snapshot of current status for rendering. */
gtasks_sync_status_t gtasks_sync_get_status(gtasks_sync_t *sync);

#endif
