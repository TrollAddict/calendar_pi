#ifndef CALENDAR_VIEW_H
#define CALENDAR_VIEW_H

#include "calendar_model.h"
#include "gl_renderer.h"
#include "event_store.h"
#include "gcal_sync.h"

/* Renders the Google-Calendar-style week (containing `selected`) as an
 * hour grid, with `events` drawn as colored blocks. `status` controls
 * a small non-blocking footer line for CONFIG_MISSING/OFFLINE; other
 * statuses draw no footer. */
void draw_week(renderer_t *rnd, cal_date_t today, cal_date_t selected,
               const gcal_event_t *events, int event_count, gcal_sync_status_t status);

/* Full-screen device-authorization prompt shown while status is
 * NEEDS_AUTH/WAITING_APPROVAL. */
void draw_auth_screen(renderer_t *rnd, const char *user_code,
                       const char *verification_url, int seconds_remaining);

#endif
