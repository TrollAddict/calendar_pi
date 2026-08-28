#define _DEFAULT_SOURCE
#include "gcal_client.h"
#include <curl/curl.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EVENTS_URL "https://www.googleapis.com/calendar/v3/calendars/primary/events"
#define HTTP_TIMEOUT_SEC 15L

typedef struct {
    char *buf;
    size_t len, cap;
} dyn_body_t;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    dyn_body_t *b = (dyn_body_t *)userp;
    size_t add = size * nmemb;
    if (b->len + add + 1 > b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 16384;
        while (new_cap < b->len + add + 1) new_cap *= 2;
        char *n = realloc(b->buf, new_cap);
        if (!n) return 0; /* tells curl the transfer failed */
        b->buf = n;
        b->cap = new_cap;
    }
    memcpy(b->buf + b->len, contents, add);
    b->len += add;
    b->buf[b->len] = '\0';
    return add;
}

static void format_rfc3339_utc(time_t t, char *out, size_t cap) {
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

/* Parses "YYYY-MM-DDTHH:MM:SS[.fff](Z|+HH:MM|-HH:MM)" to a UTC epoch. */
static int parse_rfc3339(const char *s, time_t *out) {
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    int y, mo, d, h, mi, sec;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec) != 6) return -1;
    tmv.tm_year = y - 1900;
    tmv.tm_mon = mo - 1;
    tmv.tm_mday = d;
    tmv.tm_hour = h;
    tmv.tm_min = mi;
    tmv.tm_sec = sec;

    const char *p = s + 19; /* just past "YYYY-MM-DDTHH:MM:SS" */
    while (*p && *p != 'Z' && *p != '+' && *p != '-') p++;

    long offset_sec = 0;
    if (*p == '+' || *p == '-') {
        int sign = (*p == '-') ? -1 : 1;
        int oh = 0, om = 0;
        sscanf(p + 1, "%2d:%2d", &oh, &om);
        offset_sec = sign * (oh * 3600 + om * 60);
    }

    time_t epoch = timegm(&tmv);
    if (epoch == (time_t)-1) return -1;
    *out = epoch - offset_sec;
    return 0;
}

/* Parses "YYYY-MM-DD" (an all-day event boundary) as local midnight. */
static int parse_date_local_midnight(const char *s, time_t *out) {
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    int y, mo, d;
    if (sscanf(s, "%4d-%2d-%2d", &y, &mo, &d) != 3) return -1;
    tmv.tm_year = y - 1900;
    tmv.tm_mon = mo - 1;
    tmv.tm_mday = d;
    tmv.tm_isdst = -1;
    time_t t = mktime(&tmv);
    if (t == (time_t)-1) return -1;
    *out = t;
    return 0;
}

/* Fills out and all_day from a start/end object ({"dateTime": ...} or
 * {"date": ...}). Returns 0 on success. */
static int parse_event_boundary(cJSON *obj, time_t *out, int *all_day) {
    cJSON *date_time = cJSON_GetObjectItem(obj, "dateTime");
    if (cJSON_IsString(date_time)) {
        *all_day = 0;
        return parse_rfc3339(date_time->valuestring, out);
    }
    cJSON *date = cJSON_GetObjectItem(obj, "date");
    if (cJSON_IsString(date)) {
        *all_day = 1;
        return parse_date_local_midnight(date->valuestring, out);
    }
    return -1;
}

int gcal_fetch_events(const char *access_token, time_t range_start, time_t range_end,
                       gcal_event_t *out, int max_out) {
    char time_min[32], time_max[32];
    format_rfc3339_utc(range_start, time_min, sizeof(time_min));
    format_rfc3339_utc(range_end, time_max, sizeof(time_max));

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    char *esc_min = curl_easy_escape(curl, time_min, 0);
    char *esc_max = curl_easy_escape(curl, time_max, 0);
    char url[1024];
    snprintf(url, sizeof(url),
             "%s?timeMin=%s&timeMax=%s&singleEvents=true&orderBy=startTime&maxResults=250",
             EVENTS_URL, esc_min ? esc_min : "", esc_max ? esc_max : "");
    curl_free(esc_min);
    curl_free(esc_max);

    char auth_header[2200];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", access_token);
    struct curl_slist *headers = curl_slist_append(NULL, auth_header);

    dyn_body_t body = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SEC);

    CURLcode res = curl_easy_perform(curl);
    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_status != 200 || !body.buf) {
        free(body.buf);
        return -1;
    }

    cJSON *json = cJSON_Parse(body.buf);
    free(body.buf);
    if (!json) return -1;

    cJSON *items = cJSON_GetObjectItem(json, "items");
    if (!cJSON_IsArray(items)) {
        cJSON_Delete(json);
        return -1;
    }

    int count = 0;
    int n = cJSON_GetArraySize(items);
    for (int i = 0; i < n && count < max_out; i++) {
        cJSON *item = cJSON_GetArrayItem(items, i);
        cJSON *start = cJSON_GetObjectItem(item, "start");
        cJSON *end = cJSON_GetObjectItem(item, "end");
        if (!start || !end) continue;

        gcal_event_t ev;
        memset(&ev, 0, sizeof(ev));
        int start_all_day = 0, end_all_day = 0;
        if (parse_event_boundary(start, &ev.start, &start_all_day) != 0) continue;
        if (parse_event_boundary(end, &ev.end, &end_all_day) != 0) continue;
        ev.all_day = start_all_day;

        cJSON *summary = cJSON_GetObjectItem(item, "summary");
        snprintf(ev.title, sizeof(ev.title), "%s",
                 cJSON_IsString(summary) ? summary->valuestring : "(No title)");

        cJSON *color_id = cJSON_GetObjectItem(item, "colorId");
        ev.color_id = cJSON_IsString(color_id) ? atoi(color_id->valuestring) : 0;

        out[count++] = ev;
    }

    cJSON_Delete(json);
    return count;
}
