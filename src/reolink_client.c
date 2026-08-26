#define _DEFAULT_SOURCE
#include "reolink_client.h"
#include "camera_store.h"
#include "stb_image.h"
#include <cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HTTP_TIMEOUT_SEC 10L
#define TOKEN_CAP 128

/* Cached session token, logged in once and reused across polls until it's
 * near expiry or a request fails (which invalidates it so the next
 * attempt re-logs-in). Module-level state is safe here: only
 * reolink_sync's single background thread ever calls into this file.
 *
 * Some Reolink firmware/models reject the simpler "just put user=/
 * password= on every command" approach outright (confirmed against real
 * hardware -- login via the web UI works, a brand-new user still gets
 * "login failed" via that method), so this always goes through the
 * documented cmd=Login -> token flow instead of trying the shortcut
 * first. */
static char g_token[TOKEN_CAP] = "";
static time_t g_token_expiry = 0;

typedef struct {
    unsigned char *buf;
    size_t len, cap;
} dyn_body_t;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    dyn_body_t *b = (dyn_body_t *)userp;
    size_t add = size * nmemb;
    if (b->len + add > b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 65536;
        while (new_cap < b->len + add) new_cap *= 2;
        unsigned char *n = realloc(b->buf, new_cap);
        if (!n) return 0; /* tells curl the transfer failed */
        b->buf = n;
        b->cap = new_cap;
    }
    memcpy(b->buf + b->len, contents, add);
    b->len += add;
    return add;
}

/* POSTs cmd=Login with a JSON body ({"User":{"userName":...,"password":...}}),
 * caching the returned session token (and its lease time) into
 * g_token/g_token_expiry on success. Returns 0 on success. */
static int reolink_login(const camera_config_t *cfg) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    cJSON *user = cJSON_CreateObject();
    cJSON_AddStringToObject(user, "userName", cfg->user);
    cJSON_AddStringToObject(user, "password", cfg->password);
    cJSON *param = cJSON_CreateObject();
    cJSON_AddItemToObject(param, "User", user);
    cJSON *cmd_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd_obj, "cmd", "Login");
    cJSON_AddNumberToObject(cmd_obj, "action", 0);
    cJSON_AddItemToObject(cmd_obj, "param", param);
    cJSON *root = cJSON_CreateArray();
    cJSON_AddItemToArray(root, cmd_obj);

    char *json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_body) {
        curl_easy_cleanup(curl);
        return -1;
    }

    char url[512];
    snprintf(url, sizeof(url), "http://%s/cgi-bin/api.cgi?cmd=Login", cfg->host);

    struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");

    dyn_body_t body = {0};
    char err_buf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err_buf);

    CURLcode res = curl_easy_perform(curl);
    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    cJSON_free(json_body);

    if (res != CURLE_OK) {
        fprintf(stderr, "reolink_client: login request to %s failed: %s (%s)\n", cfg->host,
                curl_easy_strerror(res), err_buf);
        free(body.buf);
        return -1;
    }
    if (http_status != 200 || !body.buf) {
        fprintf(stderr, "reolink_client: login to %s returned HTTP %ld\n", cfg->host, http_status);
        free(body.buf);
        return -1;
    }

    cJSON *json = cJSON_Parse((const char *)body.buf);
    free(body.buf);
    if (!json) return -1;

    cJSON *first = cJSON_GetArrayItem(json, 0);
    cJSON *value = first ? cJSON_GetObjectItem(first, "value") : NULL;
    cJSON *token_obj = value ? cJSON_GetObjectItem(value, "Token") : NULL;
    cJSON *name = token_obj ? cJSON_GetObjectItem(token_obj, "name") : NULL;
    cJSON *lease = token_obj ? cJSON_GetObjectItem(token_obj, "leaseTime") : NULL;

    int ok = cJSON_IsString(name) && name->valuestring[0];
    if (ok) {
        snprintf(g_token, sizeof(g_token), "%s", name->valuestring);
        int lease_sec = cJSON_IsNumber(lease) ? lease->valueint : 3600;
        /* Refresh a bit ahead of the camera's own expiry rather than
         * racing it. */
        g_token_expiry = time(NULL) + (lease_sec > 60 ? lease_sec - 60 : lease_sec);
    } else {
        cJSON *error = first ? cJSON_GetObjectItem(first, "error") : NULL;
        cJSON *detail = error ? cJSON_GetObjectItem(error, "detail") : NULL;
        fprintf(stderr, "reolink_client: login to %s rejected: %s\n", cfg->host,
                cJSON_IsString(detail) ? detail->valuestring : "unknown reason");
    }
    cJSON_Delete(json);
    return ok ? 0 : -1;
}

int reolink_fetch_snapshot(const camera_config_t *cfg, unsigned char **out_rgb, int *out_w, int *out_h) {
    if (!g_token[0] || time(NULL) >= g_token_expiry) {
        if (reolink_login(cfg) != 0) {
            g_token[0] = '\0';
            return -1;
        }
    }

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    char *esc_token = curl_easy_escape(curl, g_token, 0);

    char url[1024];
    snprintf(url, sizeof(url), "http://%s/cgi-bin/api.cgi?cmd=Snap&channel=%d&rs=%ld&token=%s", cfg->host,
             cfg->channel, (long)time(NULL), esc_token ? esc_token : "");
    curl_free(esc_token);

    dyn_body_t body = {0};
    char err_buf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err_buf);

    CURLcode res = curl_easy_perform(curl);
    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    curl_easy_cleanup(curl);

    /* Logs the host but never the URL itself -- it carries the session
     * token, which is nearly as sensitive as the password it stands in
     * for. */
    if (res != CURLE_OK) {
        fprintf(stderr, "reolink_client: request to %s failed: %s (%s)\n", cfg->host, curl_easy_strerror(res),
                err_buf);
        free(body.buf);
        return -1;
    }
    if (http_status != 200 || !body.buf) {
        fprintf(stderr, "reolink_client: %s returned HTTP %ld\n", cfg->host, http_status);
        free(body.buf);
        return -1;
    }

    /* Check the JPEG header's declared dimensions before committing to a
     * full decode -- a camera whose main stream exceeds the cap would
     * otherwise pay for a full width*height decode every poll only to
     * have the result thrown away below. */
    int info_w = 0, info_h = 0, info_comp = 0;
    if (!stbi_info_from_memory(body.buf, (int)body.len, &info_w, &info_h, &info_comp)) {
        /* Not an image at all -- most likely a JSON error body, e.g. the
         * token expired/was invalidated server-side between polls.
         * Invalidate the cached token so the next poll re-logs-in rather
         * than repeating the same failure indefinitely. Log a safe
         * printable preview of what actually came back so this is
         * diagnosable from journalctl alone. */
        g_token[0] = '\0';
        char preview[121];
        size_t n = body.len < sizeof(preview) - 1 ? body.len : sizeof(preview) - 1;
        memcpy(preview, body.buf, n);
        preview[n] = '\0';
        for (size_t i = 0; i < n; i++) {
            if (preview[i] < 32 || preview[i] > 126) preview[i] = '.';
        }
        fprintf(stderr, "reolink_client: %s's response (%zu bytes) isn't a decodable image -- starts: %s\n",
                cfg->host, body.len, preview);
        free(body.buf);
        return -1;
    }
    if (info_w > CAMERA_MAX_W || info_h > CAMERA_MAX_H) {
        fprintf(stderr,
                "reolink_client: snapshot is %dx%d, above the %dx%d cap -- configure a lower "
                "resolution on the camera (see docs/REOLINK_SETUP.md)\n",
                info_w, info_h, CAMERA_MAX_W, CAMERA_MAX_H);
        free(body.buf);
        return -1;
    }

    int w = 0, h = 0, channels_in_file = 0;
    unsigned char *rgb = stbi_load_from_memory(body.buf, (int)body.len, &w, &h, &channels_in_file, 3);
    free(body.buf);
    if (!rgb) {
        fprintf(stderr, "reolink_client: JPEG decode failed: %s\n", stbi_failure_reason());
        return -1;
    }

    /* Defensive: the full decode should agree with the header we already
     * checked, but re-verify before handing the buffer back rather than
     * trusting that invariant blindly. */
    if (w > CAMERA_MAX_W || h > CAMERA_MAX_H) {
        stbi_image_free(rgb);
        return -1;
    }

    *out_rgb = rgb;
    *out_w = w;
    *out_h = h;
    return 0;
}
