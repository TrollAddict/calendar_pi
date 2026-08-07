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

    vertex_t *rect_verts;
    int rect_count, rect_cap;
    vertex_t *text_verts;
    int text_count, text_cap;
} renderer_t;

int renderer_init(renderer_t *rnd, int screen_w, int screen_h);
void renderer_destroy(renderer_t *rnd);

void renderer_begin_frame(renderer_t *rnd);
void renderer_end_frame(renderer_t *rnd);

void renderer_add_rect(renderer_t *rnd, float x, float y, float w, float h,
                        float red, float g, float b, float a);
void renderer_add_rect_outline(renderer_t *rnd, float x, float y, float w, float h,
                                float thickness, float red, float g, float b, float a);
void renderer_add_text(renderer_t *rnd, float x, float y, int scale, const char *text,
                        float red, float g, float b, float a);
void renderer_add_text_centered(renderer_t *rnd, float cx, float y, int scale, const char *text,
                                 float red, float g, float b, float a);

#endif
