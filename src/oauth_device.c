#define _DEFAULT_SOURCE
#include "oauth_device.h"
#include <curl/curl.h>
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DEVICE_CODE_URL "https://oauth2.googleapis.com/device/code"
#define TOKEN_URL "https://oauth2.googleapis.com/token"
#define HTTP_BODY_CAP 8192
#define HTTP_TIMEOUT_SEC 15L

typedef struct {
    char buf[HTTP_BODY_CAP];
    size_t len;
} http_body_t;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    http_body_t *body = (http_body_t *)userp;
    size_t add = size * nmemb;
    size_t space = (sizeof(body->buf) - 1) - body->len;
    if (add > space) add = space;
    memcpy(body->buf + body->len, contents, add);
    body->len += add;
    body->buf[body->len] = '\0';
    return size * nmemb; /* report full size consumed even if truncated */
}

/* Appends "key=url-escaped-val" (with a leading '&' if not first) to
 * form, which must have FORM_CAP bytes of room. */
#define FORM_CAP 2048
static void append_form_field(CURL *curl, char *form, const char *key, const char *val, int *first) {
    char *escaped = curl_easy_escape(curl, val, 0);
    if (!escaped) return;
    size_t used = strlen(form);
    snprintf(form + used, FORM_CAP - used, "%s%s=%s", *first ? "" : "&", key, escaped);
    curl_free(escaped);
    *first = 0;
}

/* POSTs form (application/x-www-form-urlencoded) to url and fills
 * body with the response. Returns 0 if the transport succeeded
 * (regardless of HTTP status -- Google's OAuth errors are still JSON
 * bodies worth parsing, e.g. authorization_pending). */
static int http_post_form(const char *url, const char *form, http_body_t *body) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    body->len = 0;
    body->buf[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return res == CURLE_OK ? 0 : -1;
}

static const char *json_str(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int json_int(cJSON *obj, const char *key, int fallback) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

int oauth_device_start(const char *client_id, const char *scope, oauth_device_code_t *out) {
    memset(out, 0, sizeof(*out));

    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    char form[FORM_CAP] = {0};
    int first = 1;
    append_form_field(curl, form, "client_id", client_id, &first);
    append_form_field(curl, form, "scope", scope, &first);
    curl_easy_cleanup(curl);

    http_body_t body;
    if (http_post_form(DEVICE_CODE_URL, form, &body) != 0) return -1;

    cJSON *json = cJSON_Parse(body.buf);
    if (!json) return -1;

    const char *device_code = json_str(json, "device_code");
    const char *user_code = json_str(json, "user_code");
    const char *verification_url = json_str(json, "verification_url");
    if (!verification_url) verification_url = json_str(json, "verification_uri"); /* RFC 8628 key */
    int interval = json_int(json, "interval", 5);
    int expires_in = json_int(json, "expires_in", 1800);

    int ok = device_code && user_code && verification_url;
    if (ok) {
        snprintf(out->device_code, sizeof(out->device_code), "%s", device_code);
        snprintf(out->user_code, sizeof(out->user_code), "%s", user_code);
        snprintf(out->verification_url, sizeof(out->verification_url), "%s", verification_url);
        out->interval_sec = interval;
        out->expires_in_sec = expires_in;
    }
    cJSON_Delete(json);
    return ok ? 0 : -1;
}

/* Sleeps up to seconds, waking early (returning 1) if *stop_flag goes
 * true. Returns 0 if the full sleep elapsed. */
static int interruptible_sleep(int seconds, volatile int *stop_flag) {
    for (int i = 0; i < seconds; i++) {
        if (*stop_flag) return 1;
        struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};
        nanosleep(&ts, NULL);
    }
    return *stop_flag ? 1 : 0;
}

static int token_form_request(const char *form, oauth_tokens_t *out, char *error_out, size_t error_cap) {
    memset(out, 0, sizeof(*out));
    if (error_out && error_cap) error_out[0] = '\0';

    http_body_t body;
    if (http_post_form(TOKEN_URL, form, &body) != 0) return -1;

    cJSON *json = cJSON_Parse(body.buf);
    if (!json) return -1;

    const char *access_token = json_str(json, "access_token");
    const char *refresh_token = json_str(json, "refresh_token");
    const char *error = json_str(json, "error");
    int expires_in = json_int(json, "expires_in", 3600);

    int ok = access_token != NULL;
    if (ok) {
        snprintf(out->access_token, sizeof(out->access_token), "%s", access_token);
        if (refresh_token) snprintf(out->refresh_token, sizeof(out->refresh_token), "%s", refresh_token);
        out->expires_in_sec = expires_in;
    } else if (error && error_out) {
        snprintf(error_out, error_cap, "%s", error);
    }
    cJSON_Delete(json);
    return ok ? 0 : -1;
}

int oauth_device_poll(const char *client_id, const char *client_secret,
                       const oauth_device_code_t *code, volatile int *stop_flag,
                       oauth_tokens_t *out) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    char form[FORM_CAP] = {0};
    int first = 1;
    append_form_field(curl, form, "client_id", client_id, &first);
    append_form_field(curl, form, "client_secret", client_secret, &first);
    append_form_field(curl, form, "device_code", code->device_code, &first);
    append_form_field(curl, form, "grant_type", "urn:ietf:params:oauth:grant-type:device_code", &first);
    curl_easy_cleanup(curl);

    int interval = code->interval_sec > 0 ? code->interval_sec : 5;
    time_t deadline = time(NULL) + code->expires_in_sec;

    while (time(NULL) < deadline) {
        if (interruptible_sleep(interval, stop_flag)) return -1;

        char error[64];
        int rc = token_form_request(form, out, error, sizeof(error));
        if (rc == 0) return 0;

        if (strcmp(error, "authorization_pending") == 0) {
            continue;
        } else if (strcmp(error, "slow_down") == 0) {
            interval += 5;
            continue;
        } else {
            /* expired_token, access_denied, or a transport failure. */
            return -1;
        }
    }
    return -1;
}

int oauth_refresh(const char *client_id, const char *client_secret,
                   const char *refresh_token, oauth_tokens_t *out) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    char form[FORM_CAP] = {0};
    int first = 1;
    append_form_field(curl, form, "client_id", client_id, &first);
    append_form_field(curl, form, "client_secret", client_secret, &first);
    append_form_field(curl, form, "refresh_token", refresh_token, &first);
    append_form_field(curl, form, "grant_type", "refresh_token", &first);
    curl_easy_cleanup(curl);

    return token_form_request(form, out, NULL, 0);
}
