#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stddef.h>

#define CONFIG_STR_CAP 256

typedef struct {
    char client_id[CONFIG_STR_CAP];
    char client_secret[CONFIG_STR_CAP];
} gcal_client_config_t;

/* Reads $HOME/.config/calendar_pi/client.conf ("client_id=...",
 * "client_secret=..." lines, user-provided from Google Cloud Console).
 * Returns 0 on success, -1 if missing or malformed -- caller should
 * degrade gracefully (point the user at the setup docs), not crash. */
int config_load_client(gcal_client_config_t *out);

/* $HOME/.config/calendar_pi/token -- just the refresh token, mode 0600.
 * Returns 0 on success, -1 if absent (first run, or a wiped token). */
int token_load_refresh(char *out, size_t cap);
int token_save_refresh(const char *refresh_token);

#endif
