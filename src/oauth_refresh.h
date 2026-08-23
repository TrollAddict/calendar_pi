#ifndef OAUTH_REFRESH_H
#define OAUTH_REFRESH_H

typedef struct {
    char access_token[2048];
    char refresh_token[256];
    int expires_in_sec;
} oauth_tokens_t;

/* grant_type=refresh_token exchange. Returns 0 on success. Note the
 * response may or may not include a new refresh_token -- Google
 * usually reuses the existing one, so callers should keep the refresh
 * token they already have unless a new one comes back.
 *
 * The refresh token itself is obtained once, out of band, via
 * tools/authorize_gcal.py -- see docs/GOOGLE_CALENDAR_SETUP.md. Google's
 * OAuth device-authorization flow (RFC 8628) was tried first here but
 * doesn't support Calendar API scopes at all (confirmed against
 * Google's own docs: https://developers.google.com/identity/protocols/oauth2/limited-input-device
 * lists the full allowed-scope set, which excludes Calendar entirely),
 * so that approach was dropped in favor of a one-time Authorization
 * Code exchange done on a machine with a real browser. */
int oauth_refresh(const char *client_id, const char *client_secret,
                   const char *refresh_token, oauth_tokens_t *out);

#endif
