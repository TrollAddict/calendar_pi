#ifndef OAUTH_DEVICE_H
#define OAUTH_DEVICE_H

typedef struct {
    char device_code[128];
    char user_code[32];
    char verification_url[128];
    int interval_sec;
    int expires_in_sec;
} oauth_device_code_t;

typedef struct {
    char access_token[2048];
    char refresh_token[256];
    int expires_in_sec;
} oauth_tokens_t;

/* RFC 8628 step 1: requests a device_code/user_code pair from Google.
 * Returns 0 on success. */
int oauth_device_start(const char *client_id, const char *scope, oauth_device_code_t *out);

/* RFC 8628 step 2: polls the token endpoint at code->interval_sec
 * (backing off +5s on a "slow_down" response) until the user approves,
 * the code expires, or an unrecoverable error occurs. Checks
 * *stop_flag between polls/sleeps so shutdown isn't delayed for up to
 * code->expires_in_sec. Returns 0 on success (tokens filled), -1
 * otherwise (expiry, denial, or *stop_flag going true). */
int oauth_device_poll(const char *client_id, const char *client_secret,
                       const oauth_device_code_t *code, volatile int *stop_flag,
                       oauth_tokens_t *out);

/* grant_type=refresh_token exchange. Returns 0 on success. Note the
 * response may or may not include a new refresh_token -- Google
 * usually reuses the existing one, so callers should keep the refresh
 * token they already have unless a new one comes back. */
int oauth_refresh(const char *client_id, const char *client_secret,
                   const char *refresh_token, oauth_tokens_t *out);

#endif
