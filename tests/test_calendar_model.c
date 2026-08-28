/* Native sanity test for calendar_model.c date math.
 * Build & run on your dev machine (no cross toolchain needed):
 *   gcc -std=c11 -Wall -Wextra -o /tmp/test_calendar tests/test_calendar_model.c src/calendar_model.c
 *   /tmp/test_calendar
 */
#define _DEFAULT_SOURCE
#include "../src/calendar_model.h"
#include <stdio.h>
#include <time.h>
#include <stddef.h>

static int check_dow(int y, int m, int d) {
    struct tm tmv;
    tmv.tm_sec = 0; tmv.tm_min = 0; tmv.tm_hour = 12;
    tmv.tm_mday = d; tmv.tm_mon = m - 1; tmv.tm_year = y - 1900;
    tmv.tm_isdst = 0;
    time_t t = timegm(&tmv);
    struct tm out;
    gmtime_r(&t, &out);
    int expected = out.tm_wday;
    int got = cal_day_of_week(y, m, d);
    if (got != expected) {
        printf("FAIL day_of_week(%d-%02d-%02d): got %d expected %d\n", y, m, d, got, expected);
        return 0;
    }
    return 1;
}

int main(void) {
    int ok = 1;
    int dates[][3] = {
        {2026, 8, 5}, {2000, 1, 1}, {2024, 2, 29}, {1999, 12, 31}, {2100, 3, 1},
        {1752, 9, 14}, {2038, 1, 19}, {1970, 1, 1}, {2020, 2, 29}, {2023, 7, 4},
    };
    for (size_t i = 0; i < sizeof(dates) / sizeof(dates[0]); i++) {
        ok &= check_dow(dates[i][0], dates[i][1], dates[i][2]);
    }

    cal_date_t d = {2026, 8, 5};
    cal_add_days(&d, 30);
    if (!(d.year == 2026 && d.month == 9 && d.day == 4)) {
        printf("FAIL add_days +30 from 2026-08-05: got %d-%02d-%02d\n", d.year, d.month, d.day);
        ok = 0;
    }

    cal_date_t d2 = {2024, 2, 28};
    cal_add_days(&d2, 1);
    if (!(d2.year == 2024 && d2.month == 2 && d2.day == 29)) {
        printf("FAIL add_days +1 from 2024-02-28: got %d-%02d-%02d\n", d2.year, d2.month, d2.day);
        ok = 0;
    }

    cal_date_t d3 = {2025, 1, 1};
    cal_add_days(&d3, -1);
    if (!(d3.year == 2024 && d3.month == 12 && d3.day == 31)) {
        printf("FAIL add_days -1 from 2025-01-01: got %d-%02d-%02d\n", d3.year, d3.month, d3.day);
        ok = 0;
    }

    if (cal_days_in_month(2024, 2) != 29) { printf("FAIL days_in_month(2024,2)\n"); ok = 0; }
    if (cal_days_in_month(2023, 2) != 28) { printf("FAIL days_in_month(2023,2)\n"); ok = 0; }
    if (cal_days_in_month(2000, 2) != 29) { printf("FAIL days_in_month(2000,2)\n"); ok = 0; }
    if (cal_days_in_month(1900, 2) != 28) { printf("FAIL days_in_month(1900,2)\n"); ok = 0; }

    /* 2026-08-09 is a Sunday. */
    cal_date_t sun = {2026, 8, 9};
    cal_date_t ws;
    cal_week_start(sun, &ws);
    if (!(ws.year == 2026 && ws.month == 8 && ws.day == 9)) {
        printf("FAIL week_start of a Sunday: got %d-%02d-%02d\n", ws.year, ws.month, ws.day);
        ok = 0;
    }

    /* 2026-08-15 is a Saturday, 6 days after the same week's Sunday. */
    cal_date_t sat = {2026, 8, 15};
    cal_week_start(sat, &ws);
    if (!(ws.year == 2026 && ws.month == 8 && ws.day == 9)) {
        printf("FAIL week_start of a Saturday: got %d-%02d-%02d\n", ws.year, ws.month, ws.day);
        ok = 0;
    }

    if (ok) {
        printf("all calendar_model tests passed\n");
        return 0;
    }
    return 1;
}
