#define _DEFAULT_SOURCE
#include "oauth_refresh.h"
#include <curl/curl.h>
#include <cJSON.h>
#include <stdio.h>
#include <string.h>

#define TOKEN_URL "https://oauth2.googleapis.com/token"
#define HTTP_BODY_CAP 8192
#define HTTP_TIMEOUT_SEC 15L
#define FORM_CAP 2048

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
static void append_form_field(CURL *curl, char *form, const char *key, const char *val, int *first) {
    char *escaped = curl_easy_escape(curl, val, 0);
    if (!escaped) return;
    size_t used = strlen(form);
    snprintf(form + used, FORM_CAP - used, "%s%s=%s", *first ? "" : "&", key, escaped);
    curl_free(escaped);
    *first = 0;
}

static const char *json_str(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int json_int(cJSON *obj, const char *key, int fallback) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

int oauth_refresh(const char *client_id, const char *client_secret,
                   const char *refresh_token, oauth_tokens_t *out) {
    memset(out, 0, sizeof(*out));

    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    char form[FORM_CAP] = {0};
    int first = 1;
    append_form_field(curl, form, "client_id", client_id, &first);
    append_form_field(curl, form, "client_secret", client_secret, &first);
    append_form_field(curl, form, "refresh_token", refresh_token, &first);
    append_form_field(curl, form, "grant_type", "refresh_token", &first);

    http_body_t body;
    body.len = 0;
    body.buf[0] = '\0';
    char err_buf[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(curl, CURLOPT_URL, TOKEN_URL);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err_buf);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "oauth_refresh: POST %s failed: %s (%s)\n", TOKEN_URL, curl_easy_strerror(res), err_buf);
        return -1;
    }

    cJSON *json = cJSON_Parse(body.buf);
    if (!json) {
        fprintf(stderr, "oauth_refresh: response wasn't valid JSON: %s\n", body.buf);
        return -1;
    }

    const char *access_token = json_str(json, "access_token");
    const char *refreshed = json_str(json, "refresh_token");
    const char *error = json_str(json, "error");
    int expires_in = json_int(json, "expires_in", 3600);

    int ok = access_token != NULL;
    if (ok) {
        snprintf(out->access_token, sizeof(out->access_token), "%s", access_token);
        if (refreshed) snprintf(out->refresh_token, sizeof(out->refresh_token), "%s", refreshed);
        out->expires_in_sec = expires_in;
    } else {
        fprintf(stderr, "oauth_refresh: refresh failed: %s\n", error ? error : body.buf);
    }
    cJSON_Delete(json);
    return ok ? 0 : -1;
}
