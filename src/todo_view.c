#include "todo_view.h"
#include "font.h"
#include <string.h>
#include <stdio.h>

/* Truncates a copy of src into dst so it renders no wider than max_w
 * pixels at the given font scale -- same approach calendar_view.c uses
 * for event titles, duplicated locally rather than shared (see that
 * file's design note). */
static void truncate_to_width(const char *src, char *dst, size_t dst_cap, int scale, int max_w) {
    char buf[TASK_TITLE_CAP];
    snprintf(buf, sizeof(buf), "%s", src);
    size_t len = strlen(buf);
    while (len > 0 && font_text_width(buf, scale) > max_w) {
        buf[--len] = '\0';
    }
    snprintf(dst, dst_cap, "%s", buf);
}

static void draw_placeholder(renderer_t *rnd, int w, int h, const char *l1, const char *l2) {
    int scale = h / 120;
    if (scale < 1) scale = 1;
    float cy = (float)h / 2.0f - (float)(FONT_GLYPH_H * scale);
    renderer_add_text_centered(rnd, (float)w / 2.0f, cy, scale, l1, 0.75f, 0.78f, 0.88f, 1.0f);
    if (l2 && l2[0]) {
        cy += (float)(FONT_GLYPH_H * scale) + 10.0f;
        renderer_add_text_centered(rnd, (float)w / 2.0f, cy, scale, l2, 0.55f, 0.60f, 0.72f, 1.0f);
    }
}

/* Stable insertion sort: due-date-first, undated tasks last, matching
 * layout_day_events' hand-rolled insertion sort style in calendar_view.c. */
static void sort_by_due(gtask_item_t *tasks, int count) {
    for (int i = 1; i < count; i++) {
        gtask_item_t key = tasks[i];
        int j = i - 1;
        while (j >= 0) {
            int move_left;
            if (!tasks[j].has_due && key.has_due) {
                move_left = 1;
            } else if (tasks[j].has_due && key.has_due) {
                move_left = tasks[j].due > key.due;
            } else {
                move_left = 0;
            }
            if (!move_left) break;
            tasks[j + 1] = tasks[j];
            j--;
        }
        tasks[j + 1] = key;
    }
}

void draw_todo(renderer_t *rnd, int region_w, int region_h,
                const gtask_item_t *tasks, int task_count, gtasks_sync_status_t status) {
    int w = region_w;
    int h = region_h;
    int margin = w / 20;
    if (margin < 8) margin = 8;

    if (status == GTASKS_STATE_CONFIG_MISSING) {
        draw_placeholder(rnd, w, h, "TO-DO NOT CONFIGURED", "SEE DOCS/GOOGLE_CALENDAR_SETUP.MD");
        return;
    }
    if (status == GTASKS_STATE_NEEDS_AUTH) {
        draw_placeholder(rnd, w, h, "TO-DO NOT YET AUTHORIZED", "SEE DOCS/GOOGLE_CALENDAR_SETUP.MD");
        return;
    }

    int title_scale = h / 200;
    if (title_scale < 1) title_scale = 1;
    title_scale += 1;
    renderer_add_text(rnd, (float)margin, (float)margin, title_scale, "TO-DO", 0.92f, 0.92f, 0.95f, 1.0f);

    int footer_h = h / 30;
    if (footer_h < 14) footer_h = 14;
    int list_top = margin + FONT_GLYPH_H * title_scale + 10;
    int list_bottom = h - margin - (status == GTASKS_STATE_OFFLINE ? footer_h : 0);

    int row_scale = h / 260;
    if (row_scale < 1) row_scale = 1;
    int box = FONT_GLYPH_H * row_scale;
    int row_h = box + 6;
    int max_rows = (list_bottom - list_top) / row_h;
    if (max_rows < 0) max_rows = 0;

    if (task_count == 0) {
        if (max_rows > 0) {
            renderer_add_text(rnd, (float)margin, (float)list_top, row_scale, "NOTHING TO DO", 0.55f, 0.60f,
                               0.72f, 1.0f);
        }
    } else {
        gtask_item_t sorted[TASK_STORE_MAX];
        int n = task_count > TASK_STORE_MAX ? TASK_STORE_MAX : task_count;
        memcpy(sorted, tasks, (size_t)n * sizeof(gtask_item_t));
        sort_by_due(sorted, n);

        int shown = n <= max_rows ? n : (max_rows > 0 ? max_rows - 1 : 0);
        int title_max_w = w - margin * 2 - box - 6;

        for (int row = 0; row < shown; row++) {
            const gtask_item_t *t = &sorted[row];
            float y = (float)(list_top + row * row_h);
            float box_x = (float)margin;

            if (t->completed) {
                renderer_add_rect(rnd, box_x, y, (float)box, (float)box, 0.20f, 0.65f, 0.35f, 0.9f);
            } else {
                renderer_add_rect_outline(rnd, box_x, y, (float)box, (float)box, 1.0f, 0.55f, 0.60f, 0.72f, 1.0f);
            }

            char title[TASK_TITLE_CAP];
            truncate_to_width(t->title, title, sizeof(title), row_scale, title_max_w);

            float text_x = box_x + (float)box + 6.0f;
            if (t->completed) {
                renderer_add_text(rnd, text_x, y, row_scale, title, 0.45f, 0.48f, 0.55f, 1.0f);
                int tw = font_text_width(title, row_scale);
                renderer_add_rect(rnd, text_x, y + (float)box / 2.0f, (float)tw, 1.0f, 0.45f, 0.48f, 0.55f, 1.0f);
            } else {
                renderer_add_text(rnd, text_x, y, row_scale, title, 0.90f, 0.90f, 0.92f, 1.0f);
            }
        }

        if (n > max_rows && max_rows > 0) {
            float y = (float)(list_top + (max_rows - 1) * row_h);
            char overflow[24];
            snprintf(overflow, sizeof(overflow), "+%d MORE", n - (max_rows - 1));
            renderer_add_text(rnd, (float)margin, y, row_scale, overflow, 0.55f, 0.60f, 0.72f, 1.0f);
        }
    }

    if (status == GTASKS_STATE_OFFLINE) {
        renderer_add_text(rnd, (float)margin, (float)(h - footer_h + 2), 1, "OFFLINE - SHOWING CACHED TASKS",
                           0.55f, 0.55f, 0.60f, 1.0f);
    }
}
