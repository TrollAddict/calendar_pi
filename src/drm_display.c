#define _DEFAULT_SOURCE
#include "drm_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <drm_fourcc.h>

typedef struct {
    uint32_t fb_id;
} fb_userdata_t;

static void fb_destroy_callback(struct gbm_bo *bo, void *data) {
    fb_userdata_t *fb = data;
    if (fb->fb_id) {
        int fd = gbm_device_get_fd(gbm_bo_get_device(bo));
        drmModeRmFB(fd, fb->fb_id);
    }
    free(fb);
}

static uint32_t get_fb_for_bo(int fd, struct gbm_bo *bo) {
    fb_userdata_t *fb = gbm_bo_get_user_data(bo);
    if (fb) return fb->fb_id;

    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t handle = gbm_bo_get_handle(bo).u32;

    fb = calloc(1, sizeof(*fb));
    uint32_t handles[4] = {handle, 0, 0, 0};
    uint32_t strides[4] = {stride, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};

    int ret = drmModeAddFB2(fd, width, height, DRM_FORMAT_XRGB8888,
                             handles, strides, offsets, &fb->fb_id, 0);
    if (ret) {
        fprintf(stderr, "drmModeAddFB2 failed: %s\n", strerror(errno));
        free(fb);
        return 0;
    }
    gbm_bo_set_user_data(bo, fb, fb_destroy_callback);
    return fb->fb_id;
}

static int find_connector_and_mode(drm_display_t *d) {
    drmModeRes *res = drmModeGetResources(d->fd);
    if (!res) {
        fprintf(stderr, "drmModeGetResources failed\n");
        return -1;
    }

    drmModeConnector *connector = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(d->fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            connector = c;
            break;
        }
        if (c) drmModeFreeConnector(c);
    }
    if (!connector) {
        fprintf(stderr, "no connected display found on this DRM device\n");
        drmModeFreeResources(res);
        return -1;
    }

    d->connector_id = connector->connector_id;
    d->mode = connector->modes[0];
    for (int i = 0; i < connector->count_modes; i++) {
        if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            d->mode = connector->modes[i];
            break;
        }
    }
    d->width = (int)d->mode.hdisplay;
    d->height = (int)d->mode.vdisplay;

    drmModeEncoder *encoder = NULL;
    if (connector->encoder_id) {
        encoder = drmModeGetEncoder(d->fd, connector->encoder_id);
    }

    uint32_t crtc_id = 0;
    if (encoder && encoder->crtc_id) {
        crtc_id = encoder->crtc_id;
    } else {
        for (int i = 0; i < connector->count_encoders && !crtc_id; i++) {
            drmModeEncoder *e = drmModeGetEncoder(d->fd, connector->encoders[i]);
            if (!e) continue;
            for (int j = 0; j < res->count_crtcs; j++) {
                if (e->possible_crtcs & (1u << j)) {
                    crtc_id = res->crtcs[j];
                    break;
                }
            }
            drmModeFreeEncoder(e);
        }
    }
    if (encoder) drmModeFreeEncoder(encoder);

    if (!crtc_id) {
        fprintf(stderr, "no usable crtc found for connector %u\n", d->connector_id);
        drmModeFreeConnector(connector);
        drmModeFreeResources(res);
        return -1;
    }
    d->crtc_id = crtc_id;

    drmModeFreeConnector(connector);
    drmModeFreeResources(res);
    return 0;
}

int drm_display_init(drm_display_t *d, const char *device_path) {
    memset(d, 0, sizeof(*d));
    d->first_frame = 1;
    d->egl_display = EGL_NO_DISPLAY;
    d->egl_context = EGL_NO_CONTEXT;
    d->egl_surface = EGL_NO_SURFACE;

    d->fd = open(device_path, O_RDWR | O_CLOEXEC);
    if (d->fd < 0) {
        fprintf(stderr, "failed to open %s: %s\n", device_path, strerror(errno));
        return -1;
    }

    if (find_connector_and_mode(d) != 0) {
        close(d->fd);
        return -1;
    }

    d->saved_crtc = drmModeGetCrtc(d->fd, d->crtc_id);

    d->gbm_dev = gbm_create_device(d->fd);
    if (!d->gbm_dev) {
        fprintf(stderr, "gbm_create_device failed\n");
        return -1;
    }

    d->gbm_surf = gbm_surface_create(d->gbm_dev, (uint32_t)d->width, (uint32_t)d->height,
                                      GBM_FORMAT_XRGB8888,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!d->gbm_surf) {
        fprintf(stderr, "gbm_surface_create failed\n");
        return -1;
    }

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display) {
        d->egl_display = get_platform_display(EGL_PLATFORM_GBM_KHR, d->gbm_dev, NULL);
    } else {
        d->egl_display = eglGetDisplay((EGLNativeDisplayType)d->gbm_dev);
    }
    if (d->egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "failed to get EGL display\n");
        return -1;
    }

    EGLint major, minor;
    if (!eglInitialize(d->egl_display, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed\n");
        return -1;
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE
    };
    EGLConfig config;
    EGLint num_configs = 0;
    if (!eglChooseConfig(d->egl_display, config_attribs, &config, 1, &num_configs) || num_configs < 1) {
        fprintf(stderr, "eglChooseConfig failed to find a suitable config\n");
        return -1;
    }

    EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    d->egl_context = eglCreateContext(d->egl_display, config, EGL_NO_CONTEXT, context_attribs);
    if (d->egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext failed\n");
        return -1;
    }

    d->egl_surface = eglCreateWindowSurface(d->egl_display, config,
                                             (EGLNativeWindowType)d->gbm_surf, NULL);
    if (d->egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreateWindowSurface failed\n");
        return -1;
    }

    if (!eglMakeCurrent(d->egl_display, d->egl_surface, d->egl_surface, d->egl_context)) {
        fprintf(stderr, "eglMakeCurrent failed\n");
        return -1;
    }

    return 0;
}

static void page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data) {
    (void)fd;
    (void)frame;
    (void)sec;
    (void)usec;
    int *waiting = data;
    *waiting = 0;
}

void drm_display_swap(drm_display_t *d) {
    eglSwapBuffers(d->egl_display, d->egl_surface);

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(d->gbm_surf);
    if (!bo) {
        fprintf(stderr, "gbm_surface_lock_front_buffer failed\n");
        return;
    }

    uint32_t fb_id = get_fb_for_bo(d->fd, bo);

    if (d->first_frame) {
        int ret = drmModeSetCrtc(d->fd, d->crtc_id, fb_id, 0, 0, &d->connector_id, 1, &d->mode);
        if (ret) {
            fprintf(stderr, "drmModeSetCrtc failed: %s\n", strerror(errno));
        }
        d->first_frame = 0;
    } else {
        int waiting = 1;
        int ret = drmModePageFlip(d->fd, d->crtc_id, fb_id, DRM_MODE_PAGE_FLIP_EVENT, &waiting);
        if (ret) {
            fprintf(stderr, "drmModePageFlip failed: %s\n", strerror(errno));
            waiting = 0;
        }
        while (waiting) {
            struct pollfd pfd = {.fd = d->fd, .events = POLLIN, .revents = 0};
            int pret = poll(&pfd, 1, -1);
            if (pret < 0) {
                if (errno == EINTR) continue;
                break;
            }
            drmEventContext evctx;
            memset(&evctx, 0, sizeof(evctx));
            evctx.version = DRM_EVENT_CONTEXT_VERSION;
            evctx.page_flip_handler = page_flip_handler;
            drmHandleEvent(d->fd, &evctx);
        }
    }

    if (d->current_bo) {
        gbm_surface_release_buffer(d->gbm_surf, d->current_bo);
    }
    d->current_bo = bo;
}

void drm_display_destroy(drm_display_t *d) {
    if (d->current_bo) {
        gbm_surface_release_buffer(d->gbm_surf, d->current_bo);
    }

    if (d->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(d->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (d->egl_surface != EGL_NO_SURFACE) eglDestroySurface(d->egl_display, d->egl_surface);
        if (d->egl_context != EGL_NO_CONTEXT) eglDestroyContext(d->egl_display, d->egl_context);
        eglTerminate(d->egl_display);
    }

    if (d->gbm_surf) gbm_surface_destroy(d->gbm_surf);
    if (d->gbm_dev) gbm_device_destroy(d->gbm_dev);

    if (d->saved_crtc) {
        drmModeSetCrtc(d->fd, d->saved_crtc->crtc_id, d->saved_crtc->buffer_id,
                        d->saved_crtc->x, d->saved_crtc->y,
                        &d->connector_id, 1, &d->saved_crtc->mode);
        drmModeFreeCrtc(d->saved_crtc);
    }

    if (d->fd >= 0) close(d->fd);
}
