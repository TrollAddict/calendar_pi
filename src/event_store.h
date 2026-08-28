#ifndef EVENT_STORE_H
#define EVENT_STORE_H

#include <pthread.h>
#include <time.h>

#define EVENT_STORE_MAX 256
#define EVENT_TITLE_CAP 128

typedef struct {
    time_t start, end; /* for all-day events, local midnight of each date */
    int all_day;
    int color_id; /* 0 = unset -> caller applies a default color */
    char title[EVENT_TITLE_CAP];
} gcal_event_t;

typedef struct {
    pthread_mutex_t lock;
    gcal_event_t events[EVENT_STORE_MAX];
    int count;
    time_t last_success;
    int last_fetch_ok;
} event_store_t;

void event_store_init(event_store_t *s);
void event_store_destroy(event_store_t *s);

/* Replaces the stored event list wholesale. count is clamped to
 * EVENT_STORE_MAX. */
void event_store_replace(event_store_t *s, const gcal_event_t *events, int count);

void event_store_set_fetch_status(event_store_t *s, int ok);

/* Copies up to max events into out. Returns the number copied. */
int event_store_snapshot(event_store_t *s, gcal_event_t *out, int max);

#endif
