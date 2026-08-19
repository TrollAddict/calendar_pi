#ifndef CALENDAR_MODEL_H
#define CALENDAR_MODEL_H

typedef struct {
    int year;
    int month; /* 1-12 */
    int day;   /* 1-31 */
} cal_date_t;

int cal_is_leap_year(int year);
int cal_days_in_month(int year, int month);

/* 0 = Sunday .. 6 = Saturday (proleptic Gregorian calendar) */
int cal_day_of_week(int year, int month, int day);

void cal_get_today(cal_date_t *out);

/* Add `delta` calendar days to *d, handling month/year rollover. */
void cal_add_days(cal_date_t *d, int delta);

/* Add `delta` months to (*year, *month), clamping day is the caller's job. */
void cal_add_months(int *year, int *month, int delta);

/* Sunday that starts the week containing d. */
void cal_week_start(cal_date_t d, cal_date_t *out);

extern const char *const CAL_MONTH_NAMES[12];
extern const char *const CAL_WEEKDAY_ABBR[7];

#endif
