#include "gl_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

static const char *VERTEX_SHADER_SRC =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "attribute vec4 aColor;\n"
    "uniform vec2 uScreenSize;\n"
    "varying vec2 vUV;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    float x = (aPos.x / uScreenSize.x) * 2.0 - 1.0;\n"
    "    float y = 1.0 - (aPos.y / uScreenSize.y) * 2.0;\n"
    "    gl_Position = vec4(x, y, 0.0, 1.0);\n"
    "    vUV = aUV;\n"
    "    vColor = aColor;\n"
    "}\n";

static const char *FRAGMENT_SHADER_SRC =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "varying vec4 vColor;\n"
    "uniform sampler2D uTex;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(uTex, vUV) * vColor;\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int ensure_capacity(vertex_t **arr, int *cap, int needed) {
    if (needed <= *cap) return 1;
    int new_cap = *cap ? *cap * 2 : 64;
    while (new_cap < needed) new_cap *= 2;
    vertex_t *n = realloc(*arr, (size_t)new_cap * sizeof(vertex_t));
    if (!n) return 0;
    *arr = n;
    *cap = new_cap;
    return 1;
}

int renderer_init(renderer_t *rnd, int screen_w, int screen_h) {
    memset(rnd, 0, sizeof(*rnd));
    rnd->screen_w = screen_w;
    rnd->screen_h = screen_h;

    GLuint vs = compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER_SRC);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SRC);
    if (!vs || !fs) return -1;

    rnd->program = glCreateProgram();
    glAttachShader(rnd->program, vs);
    glAttachShader(rnd->program, fs);
    glBindAttribLocation(rnd->program, 0, "aPos");
    glBindAttribLocation(rnd->program, 1, "aUV");
    glBindAttribLocation(rnd->program, 2, "aColor");
    glLinkProgram(rnd->program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(rnd->program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(rnd->program, sizeof(log), NULL, log);
        fprintf(stderr, "program link error: %s\n", log);
        return -1;
    }

    rnd->attr_pos = 0;
    rnd->attr_uv = 1;
    rnd->attr_color = 2;
    rnd->uniform_tex = glGetUniformLocation(rnd->program, "uTex");
    rnd->uniform_screen_size = glGetUniformLocation(rnd->program, "uScreenSize");

    glGenBuffers(1, &rnd->vbo);

    unsigned char white_pixel[4] = {255, 255, 255, 255};
    glGenTextures(1, &rnd->white_tex);
    glBindTexture(GL_TEXTURE_2D, rnd->white_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (font_init(&rnd->font) != 0) {
        fprintf(stderr, "font_init failed\n");
        return -1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, screen_w, screen_h);

    return 0;
}

void renderer_destroy(renderer_t *rnd) {
    free(rnd->rect_verts);
    free(rnd->text_verts);
    font_destroy(&rnd->font);
    glDeleteTextures(1, &rnd->white_tex);
    glDeleteBuffers(1, &rnd->vbo);
    glDeleteProgram(rnd->program);
}

void renderer_begin_frame(renderer_t *rnd) {
    rnd->rect_count = 0;
    rnd->text_count = 0;
    glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void push_quad(vertex_t **arr, int *count, int *cap,
                       float x, float y, float w, float h,
                       float u0, float v0, float u1, float v1,
                       float red, float g, float b, float a) {
    if (!ensure_capacity(arr, cap, *count + 6)) return;
    vertex_t *v = *arr;
    vertex_t tl = {x,     y,     u0, v0, red, g, b, a};
    vertex_t tr = {x + w, y,     u1, v0, red, g, b, a};
    vertex_t bl = {x,     y + h, u0, v1, red, g, b, a};
    vertex_t br = {x + w, y + h, u1, v1, red, g, b, a};
    v[(*count)++] = tl;
    v[(*count)++] = tr;
    v[(*count)++] = bl;
    v[(*count)++] = bl;
    v[(*count)++] = tr;
    v[(*count)++] = br;
}

void renderer_add_rect(renderer_t *rnd, float x, float y, float w, float h,
                        float red, float g, float b, float a) {
    push_quad(&rnd->rect_verts, &rnd->rect_count, &rnd->rect_cap, x, y, w, h, 0, 0, 0, 0, red, g, b, a);
}

void renderer_add_rect_outline(renderer_t *rnd, float x, float y, float w, float h,
                                float t, float red, float g, float b, float a) {
    renderer_add_rect(rnd, x, y, w, t, red, g, b, a);
    renderer_add_rect(rnd, x, y + h - t, w, t, red, g, b, a);
    renderer_add_rect(rnd, x, y, t, h, red, g, b, a);
    renderer_add_rect(rnd, x + w - t, y, t, h, red, g, b, a);
}

void renderer_add_text(renderer_t *rnd, float x, float y, int scale, const char *text,
                        float red, float g, float b, float a) {
    float pen_x = x;
    for (const char *p = text; *p; ++p) {
        float u0, v0, u1, v1;
        if (font_glyph_uv(&rnd->font, *p, &u0, &v0, &u1, &v1)) {
            float gw = (float)(FONT_GLYPH_W * scale);
            float gh = (float)(FONT_GLYPH_H * scale);
            push_quad(&rnd->text_verts, &rnd->text_count, &rnd->text_cap,
                      pen_x, y, gw, gh, u0, v0, u1, v1, red, g, b, a);
        }
        pen_x += (float)font_glyph_advance(scale);
    }
}

void renderer_add_text_centered(renderer_t *rnd, float cx, float y, int scale, const char *text,
                                 float red, float g, float b, float a) {
    int w = font_text_width(text, scale);
    renderer_add_text(rnd, cx - (float)w / 2.0f, y, scale, text, red, g, b, a);
}

static void draw_batch(renderer_t *rnd, vertex_t *verts, int count, GLuint tex) {
    if (count == 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, rnd->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * sizeof(vertex_t)), verts, GL_STREAM_DRAW);

    glVertexAttribPointer(rnd->attr_pos, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, x));
    glVertexAttribPointer(rnd->attr_uv, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, u));
    glVertexAttribPointer(rnd->attr_color, 4, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, cr));
    glEnableVertexAttribArray(rnd->attr_pos);
    glEnableVertexAttribArray(rnd->attr_uv);
    glEnableVertexAttribArray(rnd->attr_color);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(rnd->uniform_tex, 0);

    glDrawArrays(GL_TRIANGLES, 0, count);
}

void renderer_end_frame(renderer_t *rnd) {
    glUseProgram(rnd->program);
    glUniform2f(rnd->uniform_screen_size, (float)rnd->screen_w, (float)rnd->screen_h);

    draw_batch(rnd, rnd->rect_verts, rnd->rect_count, rnd->white_tex);
    draw_batch(rnd, rnd->text_verts, rnd->text_count, rnd->font.texture);
}
