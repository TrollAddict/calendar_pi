#ifndef GCAL_CLIENT_H
#define GCAL_CLIENT_H

#include "event_store.h"
#include <time.h>

/* Fetches primary-calendar events overlapping [range_start, range_end)
 * (both UTC), single-occurrence-expanded and time-ordered, into out
 * (up to max_out). Returns the count fetched, or -1 on any
 * transport/HTTP/parse error -- on -1 the caller must NOT clear
 * whatever events it already has cached. */
int gcal_fetch_events(const char *access_token, time_t range_start, time_t range_end,
                       gcal_event_t *out, int max_out);

#endif
