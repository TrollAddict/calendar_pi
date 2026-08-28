#define _DEFAULT_SOURCE
#include <stdio.h>
#include <time.h>
#include <curl/curl.h>

#include "drm_display.h"
#include "gl_renderer.h"
#include "calendar_model.h"
#include "calendar_view.h"
#include "event_store.h"
#include "gcal_sync.h"
#include "task_store.h"
#include "gtasks_sync.h"
#include "todo_view.h"
#include "camera_store.h"
#include "reolink_sync.h"
#include "camera_view.h"
#include "input.h"

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

    curl_global_init(CURL_GLOBAL_DEFAULT);
    event_store_t store;
    event_store_init(&store);
    gcal_sync_t sync;
    gcal_sync_start(&sync, &store);

    task_store_t task_store;
    task_store_init(&task_store);
    gtasks_sync_t task_sync;
    gtasks_sync_start(&task_sync, &task_store);

    camera_store_t camera_store;
    camera_store_init(&camera_store);
    reolink_sync_t camera_sync;
    reolink_sync_start(&camera_sync, &camera_store);
    camera_view_t camera_view;
    camera_view_init(&camera_view);

    cal_date_t today;
    cal_get_today(&today);
    cal_date_t selected = today;

    gcal_event_t events[EVENT_STORE_MAX];
    gtask_item_t tasks[TASK_STORE_MAX];

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

        int event_count = event_store_snapshot(&store, events, EVENT_STORE_MAX);
        gcal_sync_status_t status = gcal_sync_get_status(&sync);

        int task_count = task_store_snapshot(&task_store, tasks, TASK_STORE_MAX);
        gtasks_sync_status_t task_status = gtasks_sync_get_status(&task_sync);

        reolink_sync_status_t camera_status = reolink_sync_get_status(&camera_sync);

        int top_h = display.height / 2;
        int bottom_h = display.height - top_h;
        int left_w = display.width / 2;
        int right_w = display.width - left_w;

        renderer_begin_frame(&renderer);

        renderer_set_region(&renderer, 0, 0);
        if (status == GCAL_STATE_NEEDS_AUTH) {
            draw_needs_auth_screen(&renderer, display.width, top_h);
        } else {
            draw_week(&renderer, display.width, top_h, today, selected, events, event_count, status);
        }

        renderer_set_region(&renderer, 0, top_h);
        draw_todo(&renderer, left_w, bottom_h, tasks, task_count, task_status);

        renderer_set_region(&renderer, left_w, top_h);
        draw_camera(&renderer, &camera_view, right_w, bottom_h, &camera_store, camera_status);

        /* pane divider lines, in full-screen coordinates */
        renderer_set_region(&renderer, 0, 0);
        renderer_add_rect(&renderer, 0.0f, (float)top_h, (float)display.width, 1.0f, 0.25f, 0.27f, 0.32f, 0.5f);
        renderer_add_rect(&renderer, (float)left_w, (float)top_h, 1.0f, (float)bottom_h, 0.25f, 0.27f, 0.32f, 0.5f);

        renderer_end_frame(&renderer);
        drm_display_swap(&display);

        struct timespec ts = {.tv_sec = 0, .tv_nsec = 10L * 1000L * 1000L};
        nanosleep(&ts, NULL);
    }

    gcal_sync_stop(&sync);
    gtasks_sync_stop(&task_sync);
    reolink_sync_stop(&camera_sync);
    curl_global_cleanup();
    event_store_destroy(&store);
    task_store_destroy(&task_store);
    camera_store_destroy(&camera_store);

    input_restore();
    camera_view_destroy(&camera_view);
    renderer_destroy(&renderer);
    drm_display_destroy(&display);
    printf("\n");
    return 0;
}
