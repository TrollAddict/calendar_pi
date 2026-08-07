#ifndef DRM_DISPLAY_H
#define DRM_DISPLAY_H

#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>

typedef struct {
    int fd;
    uint32_t connector_id;
    uint32_t crtc_id;
    drmModeModeInfo mode;
    drmModeCrtc *saved_crtc;

    struct gbm_device *gbm_dev;
    struct gbm_surface *gbm_surf;
    struct gbm_bo *current_bo;

    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLSurface egl_surface;

    int width;
    int height;
    int first_frame;
} drm_display_t;

/* Opens the DRM device at device_path (e.g. "/dev/dri/card0"), picks the
 * first connected connector's preferred mode, and sets up a GBM+EGL/GLES2
 * rendering surface targeting it. Returns 0 on success. Requires no other
 * process (X11/Wayland compositor) currently holding DRM master on the
 * device. */
int drm_display_init(drm_display_t *d, const char *device_path);

/* Swaps the current GL frame to the screen, blocking until the page flip
 * (or, on the very first call, the initial modeset) completes. */
void drm_display_swap(drm_display_t *d);

void drm_display_destroy(drm_display_t *d);

#endif
