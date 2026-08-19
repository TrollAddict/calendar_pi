#include "event_store.h"
#include <string.h>

void event_store_init(event_store_t *s) {
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->lock, NULL);
}

void event_store_destroy(event_store_t *s) {
    pthread_mutex_destroy(&s->lock);
}

void event_store_replace(event_store_t *s, const gcal_event_t *events, int count) {
    if (count > EVENT_STORE_MAX) count = EVENT_STORE_MAX;
    pthread_mutex_lock(&s->lock);
    memcpy(s->events, events, (size_t)count * sizeof(gcal_event_t));
    s->count = count;
    s->last_success = time(NULL);
    pthread_mutex_unlock(&s->lock);
}

void event_store_set_fetch_status(event_store_t *s, int ok) {
    pthread_mutex_lock(&s->lock);
    s->last_fetch_ok = ok;
    pthread_mutex_unlock(&s->lock);
}

int event_store_snapshot(event_store_t *s, gcal_event_t *out, int max) {
    pthread_mutex_lock(&s->lock);
    int n = s->count < max ? s->count : max;
    memcpy(out, s->events, (size_t)n * sizeof(gcal_event_t));
    pthread_mutex_unlock(&s->lock);
    return n;
}
