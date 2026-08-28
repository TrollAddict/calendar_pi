#define _DEFAULT_SOURCE
#include "gcal_sync.h"
#include "config_store.h"
#include "oauth_refresh.h"
#include "gcal_client.h"
#include <string.h>
#include <stdio.h>

#define SYNC_INTERVAL_SEC 900 /* 15 min; see docs -- 2 HTTPS calls/cycle, well under API quotas */
#define OFFLINE_RETRY_SEC 60
#define OFFLINE_RETRY_MAX_STREAK 5
#define NEEDS_AUTH_POLL_SEC 3 /* how often to check whether a token has been dropped in place */

static void set_status(gcal_sync_t *sync, gcal_sync_status_t status) {
    pthread_mutex_lock(&sync->lock);
    sync->status = status;
    pthread_mutex_unlock(&sync->lock);
}

/* Waits up to seconds for a stop signal. Returns 1 if stop was
 * requested (caller should exit promptly), 0 if the wait elapsed. */
static int interruptible_wait(gcal_sync_t *sync, int seconds) {
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
    gcal_sync_t *sync = (gcal_sync_t *)arg;

    gcal_client_config_t cfg;
    if (config_load_client(&cfg) != 0) {
        set_status(sync, GCAL_STATE_CONFIG_MISSING);
        return NULL;
    }

    /* Google's device-authorization flow doesn't support Calendar API
     * scopes (confirmed against Google's own docs), so the refresh
     * token has to come from a one-time out-of-band exchange --
     * tools/authorize_gcal.py, run on a machine with a real browser --
     * whose output gets copied into the token file by hand. Poll for
     * it rather than negotiating anything ourselves. */
    char refresh_token[256];
    while (token_load_refresh(refresh_token, sizeof(refresh_token)) != 0) {
        set_status(sync, GCAL_STATE_NEEDS_AUTH);
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

            time_t now = time(NULL);
            time_t range_start = now - 7 * 24 * 3600;
            time_t range_end = now + 35 * 24 * 3600;
            static gcal_event_t events[EVENT_STORE_MAX];
            int n = gcal_fetch_events(tokens.access_token, range_start, range_end, events, EVENT_STORE_MAX);
            if (n >= 0) {
                event_store_replace(sync->store, events, n);
                ok = 1;
            } else {
                fprintf(stderr, "gcal_sync: events fetch failed\n");
            }
        } else {
            fprintf(stderr, "gcal_sync: token refresh failed\n");
        }

        event_store_set_fetch_status(sync->store, ok);
        set_status(sync, ok ? GCAL_STATE_SYNCING : GCAL_STATE_OFFLINE);
        consecutive_failures = ok ? 0 : consecutive_failures + 1;

        int sleep_sec = (!ok && consecutive_failures <= OFFLINE_RETRY_MAX_STREAK)
                            ? OFFLINE_RETRY_SEC
                            : SYNC_INTERVAL_SEC;
        if (interruptible_wait(sync, sleep_sec)) return NULL;
    }
    return NULL;
}

int gcal_sync_start(gcal_sync_t *sync, event_store_t *store) {
    memset(sync, 0, sizeof(*sync));
    sync->store = store;
    sync->status = GCAL_STATE_CONFIG_MISSING;
    pthread_mutex_init(&sync->lock, NULL);
    pthread_cond_init(&sync->wake, NULL);

    if (pthread_create(&sync->thread, NULL, sync_thread_main, sync) != 0) return -1;
    sync->thread_started = 1;
    return 0;
}

void gcal_sync_stop(gcal_sync_t *sync) {
    pthread_mutex_lock(&sync->lock);
    sync->stop = 1;
    pthread_cond_broadcast(&sync->wake);
    pthread_mutex_unlock(&sync->lock);

    if (sync->thread_started) pthread_join(sync->thread, NULL);

    pthread_cond_destroy(&sync->wake);
    pthread_mutex_destroy(&sync->lock);
}

gcal_sync_status_t gcal_sync_get_status(gcal_sync_t *sync) {
    pthread_mutex_lock(&sync->lock);
    gcal_sync_status_t status = sync->status;
    pthread_mutex_unlock(&sync->lock);
    return status;
}
