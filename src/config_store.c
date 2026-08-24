#define _DEFAULT_SOURCE
#include "config_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define CONFIG_PATH_CAP 512

/* Fills path with $HOME/.config/calendar_pi/<name>, creating the
 * directory tree (tolerating EEXIST) along the way. Returns 0 on
 * success. */
static int config_dir_path(char *path, size_t cap, const char *name) {
    const char *home = getenv("HOME");
    if (!home || !*home) return -1;

    char dir[CONFIG_PATH_CAP];
    snprintf(dir, sizeof(dir), "%s/.config", home);
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return -1;
    snprintf(dir, sizeof(dir), "%s/.config/calendar_pi", home);
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return -1;

    int n = snprintf(path, cap, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= cap) return -1;
    return 0;
}

static void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

int config_load_client(gcal_client_config_t *out) {
    memset(out, 0, sizeof(*out));

    char path[CONFIG_PATH_CAP];
    if (config_dir_path(path, sizeof(path), "client.conf") != 0) return -1;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[CONFIG_STR_CAP + 64];
    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        const char *eq = strchr(line, '=');
        if (!eq) continue;
        size_t key_len = (size_t)(eq - line);
        const char *val = eq + 1;
        if (key_len == 9 && strncmp(line, "client_id", 9) == 0) {
            snprintf(out->client_id, sizeof(out->client_id), "%s", val);
        } else if (key_len == 13 && strncmp(line, "client_secret", 13) == 0) {
            snprintf(out->client_secret, sizeof(out->client_secret), "%s", val);
        }
    }
    fclose(f);

    if (!out->client_id[0] || !out->client_secret[0]) return -1;
    return 0;
}

int config_load_camera(camera_config_t *out) {
    memset(out, 0, sizeof(*out));

    char path[CONFIG_PATH_CAP];
    if (config_dir_path(path, sizeof(path), "camera.conf") != 0) return -1;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[CONFIG_STR_CAP + 64];
    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        const char *eq = strchr(line, '=');
        if (!eq) continue;
        size_t key_len = (size_t)(eq - line);
        const char *val = eq + 1;
        if (key_len == 4 && strncmp(line, "host", 4) == 0) {
            snprintf(out->host, sizeof(out->host), "%s", val);
        } else if (key_len == 4 && strncmp(line, "user", 4) == 0) {
            snprintf(out->user, sizeof(out->user), "%s", val);
        } else if (key_len == 8 && strncmp(line, "password", 8) == 0) {
            snprintf(out->password, sizeof(out->password), "%s", val);
        } else if (key_len == 7 && strncmp(line, "channel", 7) == 0) {
            out->channel = atoi(val);
        }
    }
    fclose(f);

    if (!out->host[0] || !out->user[0] || !out->password[0]) return -1;
    return 0;
}

int token_load_refresh(char *out, size_t cap) {
    char path[CONFIG_PATH_CAP];
    if (config_dir_path(path, sizeof(path), "token") != 0) return -1;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    if (!fgets(out, (int)cap, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    trim_newline(out);
    return out[0] ? 0 : -1;
}

int token_save_refresh(const char *refresh_token) {
    char path[CONFIG_PATH_CAP];
    if (config_dir_path(path, sizeof(path), "token") != 0) return -1;

    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (fd < 0) return -1;
    fchmod(fd, 0600); /* defensive: umask may have loosened the create mode */

    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        return -1;
    }
    fprintf(f, "%s\n", refresh_token);
    fclose(f);
    return 0;
}
