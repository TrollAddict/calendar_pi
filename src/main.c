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

    cal_date_t today;
    cal_get_today(&today);
    cal_date_t selected = today;

    gcal_event_t events[EVENT_STORE_MAX];

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

        gcal_sync_status_t status;
        char user_code[32], verification_url[128];
        int seconds_remaining;
        gcal_sync_get_status(&sync, &status, user_code, verification_url, &seconds_remaining);

        renderer_begin_frame(&renderer);
        if (status == GCAL_STATE_NEEDS_AUTH || status == GCAL_STATE_WAITING_APPROVAL) {
            draw_auth_screen(&renderer, user_code, verification_url, seconds_remaining);
        } else {
            draw_week(&renderer, today, selected, events, event_count, status);
        }
        renderer_end_frame(&renderer);
        drm_display_swap(&display);

        struct timespec ts = {.tv_sec = 0, .tv_nsec = 10L * 1000L * 1000L};
        nanosleep(&ts, NULL);
    }

    gcal_sync_stop(&sync);
    curl_global_cleanup();
    event_store_destroy(&store);

    input_restore();
    renderer_destroy(&renderer);
    drm_display_destroy(&display);
    printf("\n");
    return 0;
}
