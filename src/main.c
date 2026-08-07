#define _DEFAULT_SOURCE
#include <stdio.h>
#include <time.h>

#include "drm_display.h"
#include "gl_renderer.h"
#include "calendar_model.h"
#include "input.h"

static void draw_calendar(renderer_t *rnd, cal_date_t today, cal_date_t selected) {
    int w = rnd->screen_w;
    int h = rnd->screen_h;

    int margin = w / 40;
    if (margin < 10) margin = 10;
    int header_h = h / 8;
    int weekday_h = h / 16;
    int grid_top = margin + header_h + weekday_h;
    int grid_w = w - margin * 2;
    int grid_h = h - grid_top - margin;

    int cols = 7;
    int rows = 6;
    float cell_w = (float)grid_w / cols;
    float cell_h = (float)grid_h / rows;

    int view_year = selected.year;
    int view_month = selected.month;

    char header[64];
    snprintf(header, sizeof(header), "%s %d", CAL_MONTH_NAMES[view_month - 1], view_year);
    int header_scale = header_h / 10;
    if (header_scale < 2) header_scale = 2;
    renderer_add_text_centered(rnd, w / 2.0f, (float)margin, header_scale, header, 0.92f, 0.92f, 0.95f, 1.0f);

    int wd_scale = weekday_h / 10;
    if (wd_scale < 1) wd_scale = 1;
    for (int i = 0; i < 7; i++) {
        float cx = (float)margin + cell_w * (float)i + cell_w / 2.0f;
        renderer_add_text_centered(rnd, cx, (float)(margin + header_h), wd_scale,
                                    CAL_WEEKDAY_ABBR[i], 0.55f, 0.65f, 0.85f, 1.0f);
    }

    int first_dow = cal_day_of_week(view_year, view_month, 1);
    int days = cal_days_in_month(view_year, view_month);
    int day_scale = (int)(cell_h / 12.0f);
    if (day_scale < 1) day_scale = 1;

    for (int day = 1; day <= days; day++) {
        int cell_index = first_dow + (day - 1);
        int col = cell_index % 7;
        int row = cell_index / 7;

        float x = (float)margin + (float)col * cell_w;
        float y = (float)grid_top + (float)row * cell_h;

        int is_today = (view_year == today.year && view_month == today.month && day == today.day);
        int is_selected = (view_year == selected.year && view_month == selected.month && day == selected.day);

        float pad = 2.0f;
        if (is_today) {
            renderer_add_rect(rnd, x + pad, y + pad, cell_w - pad * 2, cell_h - pad * 2, 0.20f, 0.45f, 0.85f, 1.0f);
        }
        if (is_selected) {
            renderer_add_rect_outline(rnd, x + pad, y + pad, cell_w - pad * 2, cell_h - pad * 2, 2.0f,
                                       0.95f, 0.75f, 0.20f, 1.0f);
        }

        char label[12];
        snprintf(label, sizeof(label), "%d", day);
        float red = is_today ? 1.0f : 0.85f;
        float g = is_today ? 1.0f : 0.85f;
        float b = is_today ? 1.0f : 0.90f;
        renderer_add_text(rnd, x + 6.0f, y + 6.0f, day_scale, label, red, g, b, 1.0f);
    }
}

int main(int argc, char **argv) {
    const char *device_path = "/dev/dri/card0";
    if (argc > 1) device_path = argv[1];

    drm_display_t display;
    if (drm_display_init(&display, device_path) != 0) {
        fprintf(stderr, "failed to initialize display on %s\n", device_path);
        return 1;
    }

    renderer_t renderer;
    if (renderer_init(&renderer, display.width, display.height) != 0) {
        fprintf(stderr, "failed to initialize renderer\n");
        drm_display_destroy(&display);
        return 1;
    }

    /* No controlling terminal (e.g. launched by systemd at boot) is not
     * fatal: run in passive, non-interactive display-only mode instead. */
    int interactive = (input_init() == 0);
    if (!interactive) {
        fprintf(stderr, "no controlling terminal; running in passive display-only mode\n");
    }

    cal_date_t today;
    cal_get_today(&today);
    cal_date_t selected = today;

    printf("calendar_pi running on %s (%dx%d)\n", device_path, display.width, display.height);
    if (interactive) {
        printf("left/right = day, up/down = week, enter = jump to today, q = quit\n");
    }

    int running = 1;
    while (running) {
        /* Cheap enough to do every frame; keeps a long-running (e.g.
         * boot-persistent) instance correct across midnight rollover. */
        cal_get_today(&today);

        if (interactive) {
            input_event_t ev = input_poll();
            switch (ev) {
                case INPUT_LEFT:
                    cal_add_days(&selected, -1);
                    break;
                case INPUT_RIGHT:
                    cal_add_days(&selected, 1);
                    break;
                case INPUT_UP:
                    cal_add_days(&selected, -7);
                    break;
                case INPUT_DOWN:
                    cal_add_days(&selected, 7);
                    break;
                case INPUT_SELECT:
                    selected = today;
                    break;
                case INPUT_QUIT:
                    running = 0;
                    break;
                case INPUT_NONE:
                default:
                    break;
            }
        } else {
            selected = today;
        }

        renderer_begin_frame(&renderer);
        draw_calendar(&renderer, today, selected);
        renderer_end_frame(&renderer);
        drm_display_swap(&display);

        struct timespec ts = {.tv_sec = 0, .tv_nsec = 10L * 1000L * 1000L};
        nanosleep(&ts, NULL);
    }

    input_restore();
    renderer_destroy(&renderer);
    drm_display_destroy(&display);
    printf("\n");
    return 0;
}
