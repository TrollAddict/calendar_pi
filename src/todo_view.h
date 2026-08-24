#ifndef TODO_VIEW_H
#define TODO_VIEW_H

#include "gl_renderer.h"
#include "task_store.h"
#include "gtasks_sync.h"

/* Renders the to-do list (checkbox + title per task, due-date-first,
 * undated last) within a region_w x region_h pane. `status` drives a
 * placeholder screen instead of the list for CONFIG_MISSING/NEEDS_AUTH,
 * and a small non-blocking footer for OFFLINE. */
void draw_todo(renderer_t *rnd, int region_w, int region_h,
                const gtask_item_t *tasks, int task_count, gtasks_sync_status_t status);

#endif
