#define _DEFAULT_SOURCE
#include "calendar_view.h"
#include "font.h"
#include "gcal_colors.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define VISIBLE_HOUR_START 6
#define VISIBLE_HOUR_END 22 /* window is [6,22), 16 hours */
#define ALLDAY_MAX_BARS 3
#define MAX_EVENTS_PER_DAY 32

static time_t day_start_local(cal_date_t d) {
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = d.year - 1900;
    tmv.tm_mon = d.month - 1;
    tmv.tm_mday = d.day;
    tmv.tm_isdst = -1;
    return mktime(&tmv);
}

static int same_date(cal_date_t a, cal_date_t b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

static void format_hour_label(int hour24, char *out, size_t cap) {
    int h12 = hour24 % 12;
    if (h12 == 0) h12 = 12;
    snprintf(out, cap, "%d%s", h12, hour24 < 12 ? "A" : "P");
}

/* Truncates a copy of src into dst so it renders no wider than max_w
 * pixels at the given font scale (used instead of glScissor clipping
 * -- see calendar_view design notes). */
static void truncate_to_width(const char *src, char *dst, size_t dst_cap, int scale, int max_w) {
    char buf[EVENT_TITLE_CAP];
    snprintf(buf, sizeof(buf), "%s", src);
    size_t len = strlen(buf);
    while (len > 0 && font_text_width(buf, scale) > max_w) {
        buf[--len] = '\0';
    }
    snprintf(dst, dst_cap, "%s", buf);
}

/* --- all-day events ------------------------------------------------- */

static int collect_day_allday(const gcal_event_t *events, int event_count, time_t day_start,
                               const gcal_event_t **out, int max_out) {
    time_t day_end = day_start + 24 * 3600;
    int n = 0;
    for (int i = 0; i < event_count; i++) {
        if (!events[i].all_day) continue;
        if (events[i].start < day_end && events[i].end > day_start) {
            if (n < max_out) out[n] = &events[i];
            n++;
        }
    }
    return n;
}

/* --- timed events: per-day overlap layout ---------------------------- */

typedef struct {
    const gcal_event_t *ev;
    int col;
    int total_cols;
} laid_event_t;

static int collect_day_timed(const gcal_event_t *events, int event_count, time_t day_start,
                              const gcal_event_t **out, int max_out) {
    time_t day_end = day_start + 24 * 3600;
    int n = 0;
    for (int i = 0; i < event_count && n < max_out; i++) {
        if (events[i].all_day) continue;
        if (events[i].start >= day_start && events[i].start < day_end) {
            out[n++] = &events[i];
        }
    }
    for (int i = 1; i < n; i++) {
        const gcal_event_t *key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j]->start > key->start) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return n;
}

/* Greedy interval-graph coloring (like the classic "minimum meeting
 * rooms" algorithm) assigns each event the lowest-numbered column
 * whose previous occupant has already ended, then a second pass over
 * maximal overlap-connected clusters gives every event in a cluster
 * an even total_cols so it can be drawn at column_width/total_cols --
 * not Google's own width-maximizing layout, but visually close and
 * far simpler. */
static int layout_day_events(const gcal_event_t *events, int event_count, time_t day_start,
                              laid_event_t *out, int max_out) {
    const gcal_event_t *day_ev[MAX_EVENTS_PER_DAY];
    int n = collect_day_timed(events, event_count, day_start, day_ev, MAX_EVENTS_PER_DAY);
    if (n > max_out) n = max_out;

    time_t column_end[MAX_EVENTS_PER_DAY];
    int cols_used = 0;
    for (int i = 0; i < n; i++) {
        out[i].ev = day_ev[i];
        int assigned = -1;
        for (int c = 0; c < cols_used; c++) {
            if (column_end[c] <= day_ev[i]->start) {
                assigned = c;
                break;
            }
        }
        if (assigned < 0) assigned = cols_used++;
        out[i].col = assigned;
        column_end[assigned] = day_ev[i]->end;
    }

    int cluster_begin = 0;
    time_t running_max_end = n > 0 ? day_ev[0]->end : 0;
    for (int i = 1; i <= n; i++) {
        int cluster_ends_here = (i == n) || (day_ev[i]->start >= running_max_end);
        if (cluster_ends_here) {
            int cluster_cols = 0;
            for (int j = cluster_begin; j < i; j++) {
                if (out[j].col + 1 > cluster_cols) cluster_cols = out[j].col + 1;
            }
            for (int j = cluster_begin; j < i; j++) out[j].total_cols = cluster_cols;
            if (i < n) {
                cluster_begin = i;
                running_max_end = day_ev[i]->end;
            }
        } else if (day_ev[i]->end > running_max_end) {
            running_max_end = day_ev[i]->end;
        }
    }
    return n;
}

/* --- main view -------------------------------------------------------- */

void draw_week(renderer_t *rnd, cal_date_t today, cal_date_t selected,
               const gcal_event_t *events, int event_count, gcal_sync_status_t status) {
    int w = rnd->screen_w;
    int h = rnd->screen_h;

    int margin = w / 40;
    if (margin < 10) margin = 10;

    cal_date_t week_start;
    cal_week_start(selected, &week_start);

    cal_date_t days[7];
    time_t day_starts[7];
    int today_col = -1;
    for (int i = 0; i < 7; i++) {
        days[i] = week_start;
        cal_add_days(&days[i], i);
        day_starts[i] = day_start_local(days[i]);
        if (same_date(days[i], today)) today_col = i;
    }

    /* how many all-day bars (0..ALLDAY_MAX_BARS) does the tallest
     * column need this week -- strip height collapses to 0 if none. */
    int max_allday_rows = 0;
    for (int i = 0; i < 7; i++) {
        const gcal_event_t *tmp[8];
        int n = collect_day_allday(events, event_count, day_starts[i], tmp, 8);
        int rows = n <= ALLDAY_MAX_BARS ? n : ALLDAY_MAX_BARS;
        if (rows > max_allday_rows) max_allday_rows = rows;
    }

    int top_label_h = h / 16;
    if (top_label_h < 14) top_label_h = 14;
    int header_h = h / 10;
    if (header_h < 24) header_h = 24;
    int allday_bar_h = h / 45;
    if (allday_bar_h < 14) allday_bar_h = 14;
    int allday_strip_h = max_allday_rows > 0 ? max_allday_rows * allday_bar_h + 4 : 0;
    int footer_h = h / 30;
    if (footer_h < 16) footer_h = 16;

    int gutter_w = w / 14;
    if (gutter_w < 36) gutter_w = 36;

    int header_top = margin + top_label_h;
    int allday_top = header_top + header_h;
    int grid_top = allday_top + allday_strip_h;
    int grid_left = margin + gutter_w;
    int grid_w = w - margin - grid_left;
    int grid_h = h - grid_top - margin - footer_h;
    if (grid_h < 1) grid_h = 1;

    float col_w = (float)grid_w / 7.0f;
    int hour_count = VISIBLE_HOUR_END - VISIBLE_HOUR_START;
    float hour_px = (float)grid_h / (float)hour_count;

    /* month/year label, spanning both months if the week crosses one */
    cal_date_t week_end = days[6];
    char top_label[80];
    if (week_start.month == week_end.month && week_start.year == week_end.year) {
        snprintf(top_label, sizeof(top_label), "%s %d", CAL_MONTH_NAMES[week_start.month - 1], week_start.year);
    } else if (week_start.year == week_end.year) {
        snprintf(top_label, sizeof(top_label), "%s - %s %d", CAL_MONTH_NAMES[week_start.month - 1],
                 CAL_MONTH_NAMES[week_end.month - 1], week_end.year);
    } else {
        snprintf(top_label, sizeof(top_label), "%s %d - %s %d", CAL_MONTH_NAMES[week_start.month - 1],
                 week_start.year, CAL_MONTH_NAMES[week_end.month - 1], week_end.year);
    }
    int top_scale = top_label_h / 10;
    if (top_scale < 1) top_scale = 1;
    renderer_add_text_centered(rnd, (float)w / 2.0f, (float)margin, top_scale, top_label, 0.92f, 0.92f, 0.95f, 1.0f);

    /* hour grid lines + labels */
    int hour_label_scale = 1;
    for (int hr = VISIBLE_HOUR_START; hr <= VISIBLE_HOUR_END; hr++) {
        float y = (float)grid_top + (float)(hr - VISIBLE_HOUR_START) * hour_px;
        renderer_add_rect(rnd, (float)grid_left, y, (float)grid_w, 1.0f, 0.30f, 0.32f, 0.38f, 0.6f);
        if (hr < VISIBLE_HOUR_END) {
            char label[8];
            format_hour_label(hr, label, sizeof(label));
            renderer_add_text(rnd, (float)margin, y + 2.0f, hour_label_scale, label, 0.55f, 0.60f, 0.72f, 1.0f);
        }
    }

    /* day column headers + separators */
    for (int i = 0; i < 7; i++) {
        float x = (float)grid_left + col_w * (float)i;
        int is_today = same_date(days[i], today);
        int is_selected = same_date(days[i], selected);

        float pad = 2.0f;
        if (is_today) {
            renderer_add_rect(rnd, x + pad, (float)header_top + pad, col_w - pad * 2, (float)header_h - pad * 2,
                               0.20f, 0.45f, 0.85f, 1.0f);
        }
        if (is_selected) {
            renderer_add_rect_outline(rnd, x + pad, (float)header_top + pad, col_w - pad * 2,
                                       (float)header_h - pad * 2, 2.0f, 0.95f, 0.75f, 0.20f, 1.0f);
        }

        float cx = x + col_w / 2.0f;
        int wd_scale = header_h / 24;
        if (wd_scale < 1) wd_scale = 1;
        char daynum[4];
        snprintf(daynum, sizeof(daynum), "%d", days[i].day);
        renderer_add_text_centered(rnd, cx, (float)header_top + 4.0f, wd_scale, CAL_WEEKDAY_ABBR[i], 0.85f, 0.85f,
                                    0.90f, 1.0f);
        renderer_add_text_centered(rnd, cx, (float)header_top + 6.0f + (float)(FONT_GLYPH_H * wd_scale), wd_scale * 2,
                                    daynum, 0.95f, 0.95f, 0.98f, 1.0f);

        renderer_add_rect(rnd, x, (float)grid_top, 1.0f, (float)grid_h, 0.25f, 0.27f, 0.32f, 0.5f);
    }
    renderer_add_rect(rnd, (float)grid_left + (float)grid_w - 1.0f, (float)grid_top, 1.0f, (float)grid_h, 0.25f,
                       0.27f, 0.32f, 0.5f);

    /* all-day bars */
    if (allday_strip_h > 0) {
        for (int i = 0; i < 7; i++) {
            const gcal_event_t *tmp[8];
            int n = collect_day_allday(events, event_count, day_starts[i], tmp, 8);
            int shown = n <= ALLDAY_MAX_BARS ? n : ALLDAY_MAX_BARS - 1;
            float x = (float)grid_left + col_w * (float)i + 2.0f;
            float bw = col_w - 4.0f;

            for (int row = 0; row < shown; row++) {
                float y = (float)allday_top + (float)row * (float)allday_bar_h + 2.0f;
                float r, g, b;
                gcal_color_for_id(tmp[row]->color_id, &r, &g, &b);
                renderer_add_rect(rnd, x, y, bw, (float)allday_bar_h - 2.0f, r, g, b, 0.9f);
                char title[EVENT_TITLE_CAP];
                truncate_to_width(tmp[row]->title, title, sizeof(title), 1, (int)bw - 4);
                renderer_add_text(rnd, x + 2.0f, y + 2.0f, 1, title, 0.08f, 0.08f, 0.10f, 1.0f);
            }
            if (n > ALLDAY_MAX_BARS) {
                float y = (float)allday_top + (float)(ALLDAY_MAX_BARS - 1) * (float)allday_bar_h + 2.0f;
                char overflow[24];
                snprintf(overflow, sizeof(overflow), "+%d MORE", n - (ALLDAY_MAX_BARS - 1));
                renderer_add_rect(rnd, x, y, bw, (float)allday_bar_h - 2.0f, 0.30f, 0.32f, 0.38f, 0.9f);
                renderer_add_text(rnd, x + 2.0f, y + 2.0f, 1, overflow, 0.85f, 0.85f, 0.90f, 1.0f);
            }
        }
    }

    /* timed events */
    for (int i = 0; i < 7; i++) {
        laid_event_t laid[MAX_EVENTS_PER_DAY];
        int n = layout_day_events(events, event_count, day_starts[i], laid, MAX_EVENTS_PER_DAY);

        for (int e = 0; e < n; e++) {
            const gcal_event_t *ev = laid[e].ev;
            float start_hour = (float)(ev->start - day_starts[i]) / 3600.0f;
            float end_hour = (float)(ev->end - day_starts[i]) / 3600.0f;
            if (end_hour <= (float)VISIBLE_HOUR_START || start_hour >= (float)VISIBLE_HOUR_END) continue;
            if (start_hour < (float)VISIBLE_HOUR_START) start_hour = (float)VISIBLE_HOUR_START;
            if (end_hour > (float)VISIBLE_HOUR_END) end_hour = (float)VISIBLE_HOUR_END;

            float col_frac_w = col_w / (float)laid[e].total_cols;
            float x = (float)grid_left + col_w * (float)i + col_frac_w * (float)laid[e].col + 1.0f;
            float box_w = col_frac_w - 2.0f;
            if (box_w < 4.0f) box_w = 4.0f;

            float y = (float)grid_top + (start_hour - (float)VISIBLE_HOUR_START) * hour_px + 1.0f;
            float box_h = (end_hour - start_hour) * hour_px - 2.0f;
            if (box_h < 14.0f) box_h = 14.0f;

            float r, g, b;
            gcal_color_for_id(ev->color_id, &r, &g, &b);
            renderer_add_rect(rnd, x, y, box_w, box_h, r, g, b, 0.92f);

            if (box_h >= (float)(FONT_GLYPH_H + 4)) {
                char title[EVENT_TITLE_CAP];
                truncate_to_width(ev->title, title, sizeof(title), 1, (int)box_w - 4);
                renderer_add_text(rnd, x + 2.0f, y + 2.0f, 1, title, 0.08f, 0.08f, 0.10f, 1.0f);
            }
        }
    }

    /* "now" line */
    if (today_col >= 0) {
        time_t now = time(NULL);
        struct tm nowtm;
        localtime_r(&now, &nowtm);
        float now_hour = (float)nowtm.tm_hour + (float)nowtm.tm_min / 60.0f;
        if (now_hour >= (float)VISIBLE_HOUR_START && now_hour <= (float)VISIBLE_HOUR_END) {
            float y = (float)grid_top + (now_hour - (float)VISIBLE_HOUR_START) * hour_px;
            renderer_add_rect(rnd, (float)grid_left, y - 1.0f, (float)grid_w, 2.0f, 0.95f, 0.30f, 0.30f, 0.9f);
        }
    }

    /* status footer */
    const char *footer_text = NULL;
    if (status == GCAL_STATE_CONFIG_MISSING) {
        footer_text = "GOOGLE CALENDAR NOT CONFIGURED - SEE DOCS/GOOGLE_CALENDAR_SETUP.MD";
    } else if (status == GCAL_STATE_OFFLINE) {
        footer_text = "OFFLINE - SHOWING CACHED EVENTS";
    }
    if (footer_text) {
        renderer_add_text(rnd, (float)margin, (float)(h - footer_h + 2), 1, footer_text, 0.55f, 0.55f, 0.60f, 1.0f);
    }
}

void draw_needs_auth_screen(renderer_t *rnd) {
    int w = rnd->screen_w;
    int h = rnd->screen_h;

    int title_scale = h / 220;
    if (title_scale < 2) title_scale = 2;
    int body_scale = h / 140;
    if (body_scale < 2) body_scale = 2;

    /* Font has no lowercase, underscore, or tilde glyphs (see font.c),
     * so this deliberately avoids spelling out exact file paths --
     * the written setup docs have those verbatim. */
    static const char *LINES[] = {
        "GOOGLE CALENDAR NOT YET AUTHORIZED",
        "",
        "RUN THE AUTHORIZE SCRIPT ON YOUR COMPUTER",
        "THEN COPY THE TOKEN IT PRINTS TO THIS PI",
        "",
        "SEE THE SETUP DOCS FOR DETAILS",
    };
    int n_lines = (int)(sizeof(LINES) / sizeof(LINES[0]));

    float line_h = (float)(FONT_GLYPH_H * body_scale) + 10.0f;
    float total_h = line_h * (float)n_lines + (float)(FONT_GLYPH_H * title_scale);
    float cy = ((float)h - total_h) / 2.0f;

    renderer_add_text_centered(rnd, (float)w / 2.0f, cy, title_scale, LINES[0], 0.92f, 0.92f, 0.95f, 1.0f);
    cy += (float)(FONT_GLYPH_H * title_scale) + line_h;

    for (int i = 1; i < n_lines; i++) {
        if (LINES[i][0]) {
            renderer_add_text_centered(rnd, (float)w / 2.0f, cy, body_scale, LINES[i], 0.75f, 0.78f, 0.88f, 1.0f);
        }
        cy += line_h;
    }
}
