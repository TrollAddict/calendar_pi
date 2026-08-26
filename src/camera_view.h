#ifndef CAMERA_VIEW_H
#define CAMERA_VIEW_H

#include "gl_renderer.h"
#include "camera_store.h"
#include "reolink_sync.h"

typedef struct {
    GLuint texture;
    unsigned char *scratch; /* CAMERA_MAX_W*CAMERA_MAX_H*3 bytes, reused every frame */
    unsigned long last_version;
    int has_frame;
    int frame_w, frame_h;
    int max_texture_size; /* GL_MAX_TEXTURE_SIZE, queried once at init */
} camera_view_t;

/* Creates the GL texture the camera pane draws into -- must be called
 * once, after the GL context is current (i.e. after renderer_init). */
void camera_view_init(camera_view_t *cv);
void camera_view_destroy(camera_view_t *cv);

/* Uploads a new frame from store if one arrived since the last call, and
 * draws it aspect-fit (letterboxed) within a region_w x region_h pane.
 * Draws a placeholder instead of video for CONFIG_MISSING or before the
 * first frame ever arrives. */
void draw_camera(renderer_t *rnd, camera_view_t *cv, int region_w, int region_h, camera_store_t *store,
                  reolink_sync_status_t status);

#endif
