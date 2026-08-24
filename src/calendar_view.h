#ifndef CALENDAR_VIEW_H
#define CALENDAR_VIEW_H

#include "calendar_model.h"
#include "gl_renderer.h"
#include "event_store.h"
#include "gcal_sync.h"

/* Renders the Google-Calendar-style week (containing `selected`) as an
 * hour grid, with `events` drawn as colored blocks, laid out within a
 * region_w x region_h pane (see renderer_set_region). `status` controls
 * a small non-blocking footer line for CONFIG_MISSING/OFFLINE; other
 * statuses draw no footer. */
void draw_week(renderer_t *rnd, int region_w, int region_h, cal_date_t today, cal_date_t selected,
               const gcal_event_t *events, int event_count, gcal_sync_status_t status);

/* Prompt shown within a region_w x region_h pane while status is
 * NEEDS_AUTH: no refresh token file yet, so nothing to sync. Points at
 * the one-time out-of-band authorization step (tools/authorize_gcal.py). */
void draw_needs_auth_screen(renderer_t *rnd, int region_w, int region_h);

#endif
