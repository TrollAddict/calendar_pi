#define _DEFAULT_SOURCE
#include "gtasks_client.h"
#include <curl/curl.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TASKS_URL "https://tasks.googleapis.com/tasks/v1/lists/@default/tasks"
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

/* Tasks API "due" is RFC3339 but only ever carries date granularity
 * ("YYYY-MM-DDT00:00:00.000Z") -- parse just the date part as local
 * midnight, same convention event_store uses for all-day events. */
static int parse_due_date(const char *s, time_t *out) {
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

int gtasks_fetch_tasks(const char *access_token, gtask_item_t *out, int max_out) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    char url[512];
    snprintf(url, sizeof(url), "%s?showCompleted=false&showHidden=false&maxResults=100", TASKS_URL);

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
        /* An empty list omits "items" entirely -- that's success, not
         * an error. */
        cJSON_Delete(json);
        return 0;
    }

    int count = 0;
    int n = cJSON_GetArraySize(items);
    for (int i = 0; i < n && count < max_out; i++) {
        cJSON *item = cJSON_GetArrayItem(items, i);

        gtask_item_t task;
        memset(&task, 0, sizeof(task));

        cJSON *title = cJSON_GetObjectItem(item, "title");
        snprintf(task.title, sizeof(task.title), "%s",
                 cJSON_IsString(title) && title->valuestring[0] ? title->valuestring : "(NO TITLE)");

        cJSON *status = cJSON_GetObjectItem(item, "status");
        task.completed = cJSON_IsString(status) && strcmp(status->valuestring, "completed") == 0;

        cJSON *due = cJSON_GetObjectItem(item, "due");
        if (cJSON_IsString(due) && parse_due_date(due->valuestring, &task.due) == 0) {
            task.has_due = 1;
        }

        out[count++] = task;
    }

    cJSON_Delete(json);
    return count;
}
