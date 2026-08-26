#define _DEFAULT_SOURCE
#include "reolink_client.h"
#include "camera_store.h"
#include "stb_image.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HTTP_TIMEOUT_SEC 10L

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

int reolink_fetch_snapshot(const camera_config_t *cfg, unsigned char **out_rgb, int *out_w, int *out_h) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    char *esc_user = curl_easy_escape(curl, cfg->user, 0);
    char *esc_pass = curl_easy_escape(curl, cfg->password, 0);

    /* host/user/password can each be up to CONFIG_STR_CAP-1 (255) bytes
     * (config_store.h), and curl_easy_escape can expand user/password up
     * to 3x (percent-encoding); sized generously so long/special-character
     * credentials can't silently truncate the request. */
    char url[2048];
    snprintf(url, sizeof(url), "http://%s/cgi-bin/api.cgi?cmd=Snap&channel=%d&rs=%ld&user=%s&password=%s",
             cfg->host, cfg->channel, (long)time(NULL), esc_user ? esc_user : "", esc_pass ? esc_pass : "");
    curl_free(esc_user);
    curl_free(esc_pass);

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

    /* Logs the host but never the URL itself -- it carries the camera
     * password as a query param. */
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
        /* Not an image at all -- most likely an HTML login page or a JSON
         * error body (e.g. a camera that rejects the direct user=/
         * password= query-param login and wants a session-token login
         * instead; see docs/REOLINK_SETUP.md). Log a safe printable
         * preview of what actually came back so this is diagnosable from
         * journalctl alone. */
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
