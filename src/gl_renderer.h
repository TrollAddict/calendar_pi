#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include <GLES2/gl2.h>
#include "font.h"

typedef struct {
    float x, y;
    float u, v;
    float cr, cg, cb, ca;
} vertex_t;

typedef struct {
    int screen_w, screen_h;

    GLuint program;
    GLuint vbo;
    GLint attr_pos, attr_uv, attr_color;
    GLint uniform_tex, uniform_screen_size;

    GLuint white_tex;
    font_t font;

    /* Pixel offset applied to every rect/text/image coordinate, so draw
     * functions can compute layout against a local pane size without the
     * renderer needing a real GL viewport/scissor per pane. */
    int region_x, region_y;

    vertex_t *rect_verts;
    int rect_count, rect_cap;
    vertex_t *text_verts;
    int text_count, text_cap;

    /* Single dynamic-texture image quad (the camera frame). Drawn between
     * the rect and text batches, so rect-drawn backgrounds sit behind it
     * and text labels sit on top of it. */
    GLuint image_tex;
    vertex_t *image_verts;
    int image_count, image_cap;
} renderer_t;

int renderer_init(renderer_t *rnd, int screen_w, int screen_h);
void renderer_destroy(renderer_t *rnd);

void renderer_begin_frame(renderer_t *rnd);
void renderer_end_frame(renderer_t *rnd);

/* All coordinates passed to renderer_add_rect/renderer_add_text/
 * renderer_draw_image below are relative to this offset, letting each
 * pane's draw function compute layout against its own local
 * (0,0)-origin width/height. Resets to (0,0) at the start of every
 * frame. */
void renderer_set_region(renderer_t *rnd, int x, int y);

void renderer_add_rect(renderer_t *rnd, float x, float y, float w, float h,
                        float red, float g, float b, float a);
void renderer_add_rect_outline(renderer_t *rnd, float x, float y, float w, float h,
                                float thickness, float red, float g, float b, float a);
void renderer_add_text(renderer_t *rnd, float x, float y, int scale, const char *text,
                        float red, float g, float b, float a);
void renderer_add_text_centered(renderer_t *rnd, float cx, float y, int scale, const char *text,
                                 float red, float g, float b, float a);

/* Draws a single opaque textured quad (e.g. a decoded camera frame) using
 * the full 0..1 UV range of `tex`. Only one image quad exists per frame --
 * calling this again before the next renderer_begin_frame replaces it. */
void renderer_draw_image(renderer_t *rnd, GLuint tex, float x, float y, float w, float h);

#endif
