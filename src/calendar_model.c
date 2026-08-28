#define _DEFAULT_SOURCE
#include "calendar_model.h"
#include <time.h>

const char *const CAL_MONTH_NAMES[12] = {
    "JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
    "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"
};

const char *const CAL_WEEKDAY_ABBR[7] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

int cal_is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int cal_days_in_month(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && cal_is_leap_year(year)) return 29;
    return days[month - 1];
}

/* Zeller's congruence (Gregorian). Internal h: 0=Saturday..6=Friday,
 * remapped below to the conventional 0=Sunday..6=Saturday. */
int cal_day_of_week(int year, int month, int day) {
    int y = year;
    int m = month;
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int k = y % 100;
    int j = y / 100;
    int h = (day + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;
}

void cal_get_today(cal_date_t *out) {
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    out->year = lt.tm_year + 1900;
    out->month = lt.tm_mon + 1;
    out->day = lt.tm_mday;
}

/* Fliegel & Van Flandern algorithm, proleptic Gregorian calendar. */
static long date_to_jdn(int y, int m, int d) {
    long a = (14 - m) / 12;
    long yy = y + 4800 - a;
    long mm = m + 12 * a - 3;
    return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}

static void jdn_to_date(long jdn, int *y, int *m, int *d) {
    long a = jdn + 32044;
    long b = (4 * a + 3) / 146097;
    long c = a - (146097 * b) / 4;
    long dd = (4 * c + 3) / 1461;
    long e = c - (1461 * dd) / 4;
    long mm = (5 * e + 2) / 153;
    *d = (int)(e - (153 * mm + 2) / 5 + 1);
    *m = (int)(mm + 3 - 12 * (mm / 10));
    *y = (int)(100 * b + dd - 4800 + mm / 10);
}

void cal_add_days(cal_date_t *d, int delta) {
    long jdn = date_to_jdn(d->year, d->month, d->day);
    jdn_to_date(jdn + delta, &d->year, &d->month, &d->day);
}

void cal_week_start(cal_date_t d, cal_date_t *out) {
    *out = d;
    cal_add_days(out, -cal_day_of_week(d.year, d.month, d.day));
}

void cal_add_months(int *year, int *month, int delta) {
    int m = *month - 1 + delta;
    int y = *year + m / 12;
    m = m % 12;
    if (m < 0) {
        m += 12;
        y -= 1;
    }
    *year = y;
    *month = m + 1;
}
