#include "camera_view.h"
#include "font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void draw_placeholder(renderer_t *rnd, int w, int h, const char *l1, const char *l2) {
    int scale = h / 120;
    if (scale < 1) scale = 1;
    float cy = (float)h / 2.0f - (float)(FONT_GLYPH_H * scale);
    renderer_add_text_centered(rnd, (float)w / 2.0f, cy, scale, l1, 0.75f, 0.78f, 0.88f, 1.0f);
    if (l2 && l2[0]) {
        cy += (float)(FONT_GLYPH_H * scale) + 10.0f;
        renderer_add_text_centered(rnd, (float)w / 2.0f, cy, scale, l2, 0.55f, 0.60f, 0.72f, 1.0f);
    }
}

void camera_view_init(camera_view_t *cv) {
    memset(cv, 0, sizeof(*cv));
    cv->scratch = malloc((size_t)CAMERA_MAX_W * (size_t)CAMERA_MAX_H * 3);
    if (!cv->scratch) {
        fprintf(stderr, "camera_view: failed to allocate frame scratch buffer -- camera pane disabled\n");
    }

    glGenTextures(1, &cv->texture);
    glBindTexture(GL_TEXTURE_2D, cv->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void camera_view_destroy(camera_view_t *cv) {
    free(cv->scratch);
    glDeleteTextures(1, &cv->texture);
}

void draw_camera(renderer_t *rnd, camera_view_t *cv, int region_w, int region_h, camera_store_t *store,
                  reolink_sync_status_t status) {
    if (status == REOLINK_STATE_CONFIG_MISSING) {
        draw_placeholder(rnd, region_w, region_h, "CAMERA NOT CONFIGURED", "SEE DOCS/REOLINK_SETUP.MD");
        return;
    }
    if (!cv->scratch) {
        draw_placeholder(rnd, region_w, region_h, "CAMERA UNAVAILABLE", "OUT OF MEMORY");
        return;
    }

    int fw = 0, fh = 0;
    if (camera_store_snapshot_if_new(store, &cv->last_version, cv->scratch, &fw, &fh)) {
        glBindTexture(GL_TEXTURE_2D, cv->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fw, fh, 0, GL_RGB, GL_UNSIGNED_BYTE, cv->scratch);
        cv->frame_w = fw;
        cv->frame_h = fh;
        cv->has_frame = 1;
    }

    if (!cv->has_frame) {
        draw_placeholder(rnd, region_w, region_h, "CONNECTING TO CAMERA...", NULL);
        return;
    }

    float frame_aspect = (float)cv->frame_w / (float)cv->frame_h;
    float region_aspect = (float)region_w / (float)region_h;
    float dst_w, dst_h;
    if (frame_aspect > region_aspect) {
        dst_w = (float)region_w;
        dst_h = dst_w / frame_aspect;
    } else {
        dst_h = (float)region_h;
        dst_w = dst_h * frame_aspect;
    }
    float dst_x = ((float)region_w - dst_w) / 2.0f;
    float dst_y = ((float)region_h - dst_h) / 2.0f;
    renderer_draw_image(rnd, cv->texture, dst_x, dst_y, dst_w, dst_h);

    if (status == REOLINK_STATE_OFFLINE) {
        renderer_add_text(rnd, 4.0f, 4.0f, 1, "OFFLINE - LAST FRAME SHOWN", 0.95f, 0.55f, 0.30f, 1.0f);
    }
}
