#define _DEFAULT_SOURCE
#include "gtasks_sync.h"
#include "config_store.h"
#include "oauth_refresh.h"
#include "gtasks_client.h"
#include <string.h>
#include <stdio.h>

#define SYNC_INTERVAL_SEC 900 /* 15 min, same cadence as gcal_sync -- tasks don't churn fast */
#define OFFLINE_RETRY_SEC 60
#define OFFLINE_RETRY_MAX_STREAK 5
#define NEEDS_AUTH_POLL_SEC 3 /* how often to check whether a token has been dropped in place */

static void set_status(gtasks_sync_t *sync, gtasks_sync_status_t status) {
    pthread_mutex_lock(&sync->lock);
    sync->status = status;
    pthread_mutex_unlock(&sync->lock);
}

/* Waits up to seconds for a stop signal. Returns 1 if stop was
 * requested (caller should exit promptly), 0 if the wait elapsed. */
static int interruptible_wait(gtasks_sync_t *sync, int seconds) {
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
    gtasks_sync_t *sync = (gtasks_sync_t *)arg;

    gcal_client_config_t cfg;
    if (config_load_client(&cfg) != 0) {
        set_status(sync, GTASKS_STATE_CONFIG_MISSING);
        return NULL;
    }

    /* Shares the same token file as gcal_sync -- both calendar and tasks
     * access ride on one refresh token, scoped for both at authorization
     * time (see tools/authorize_gcal.py). */
    char refresh_token[256];
    while (token_load_refresh(refresh_token, sizeof(refresh_token)) != 0) {
        set_status(sync, GTASKS_STATE_NEEDS_AUTH);
        if (interruptible_wait(sync, NEEDS_AUTH_POLL_SEC)) return NULL;
    }

    int consecutive_failures = 0;
    while (!sync->stop) {
        oauth_tokens_t tokens;
        int ok = 0;

        if (oauth_refresh(cfg.client_id, cfg.client_secret, refresh_token, &tokens) == 0) {
            if (tokens.refresh_token[0]) {
                snprintf(refresh_token, sizeof(refresh_token), "%s", tokens.refresh_token);
                token_save_refresh(refresh_token);
            }

            static gtask_item_t tasks[TASK_STORE_MAX];
            int n = gtasks_fetch_tasks(tokens.access_token, tasks, TASK_STORE_MAX);
            if (n >= 0) {
                task_store_replace(sync->store, tasks, n);
                ok = 1;
            } else {
                fprintf(stderr, "gtasks_sync: tasks fetch failed (token may need the tasks.readonly "
                                "scope -- see docs/GOOGLE_CALENDAR_SETUP.md)\n");
            }
        } else {
            fprintf(stderr, "gtasks_sync: token refresh failed\n");
        }

        task_store_set_fetch_status(sync->store, ok);
        set_status(sync, ok ? GTASKS_STATE_SYNCING : GTASKS_STATE_OFFLINE);
        consecutive_failures = ok ? 0 : consecutive_failures + 1;

        int sleep_sec = (!ok && consecutive_failures <= OFFLINE_RETRY_MAX_STREAK)
                            ? OFFLINE_RETRY_SEC
                            : SYNC_INTERVAL_SEC;
        if (interruptible_wait(sync, sleep_sec)) return NULL;
    }
    return NULL;
}

int gtasks_sync_start(gtasks_sync_t *sync, task_store_t *store) {
    memset(sync, 0, sizeof(*sync));
    sync->store = store;
    sync->status = GTASKS_STATE_CONFIG_MISSING;
    pthread_mutex_init(&sync->lock, NULL);
    pthread_cond_init(&sync->wake, NULL);

    if (pthread_create(&sync->thread, NULL, sync_thread_main, sync) != 0) return -1;
    sync->thread_started = 1;
    return 0;
}

void gtasks_sync_stop(gtasks_sync_t *sync) {
    pthread_mutex_lock(&sync->lock);
    sync->stop = 1;
    pthread_cond_broadcast(&sync->wake);
    pthread_mutex_unlock(&sync->lock);

    if (sync->thread_started) pthread_join(sync->thread, NULL);

    pthread_cond_destroy(&sync->wake);
    pthread_mutex_destroy(&sync->lock);
}

gtasks_sync_status_t gtasks_sync_get_status(gtasks_sync_t *sync) {
    pthread_mutex_lock(&sync->lock);
    gtasks_sync_status_t status = sync->status;
    pthread_mutex_unlock(&sync->lock);
    return status;
}
