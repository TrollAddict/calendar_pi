#ifndef REOLINK_CLIENT_H
#define REOLINK_CLIENT_H

#include "config_store.h"

/* Fetches and JPEG-decodes one snapshot from the camera described by
 * cfg into a freshly malloc'd RGB888 buffer (*out_rgb, caller must
 * free()), filling *out_w and *out_h. Returns 0 on success, -1 on any
 * transport/HTTP/decode error or if the decoded image exceeds
 * CAMERA_MAX_W x CAMERA_MAX_H (see camera_store.h) -- callers must not
 * clear whatever frame they already have cached on -1. */
int reolink_fetch_snapshot(const camera_config_t *cfg, unsigned char **out_rgb, int *out_w, int *out_h);

#endif
