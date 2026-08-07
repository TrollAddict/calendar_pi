#ifndef FONT_H
#define FONT_H

#include <GLES2/gl2.h>

#define FONT_GLYPH_W 5
#define FONT_GLYPH_H 7

typedef struct {
    GLuint texture;
    int tex_width;
    int tex_height;
} font_t;

/* Builds a procedural bitmap-font atlas texture (space, 0-9, A-Z only;
 * lowercase is folded to uppercase). Returns 0 on success. */
int font_init(font_t *font);
void font_destroy(font_t *font);

/* Fills the atlas UV rect (0..1) for a glyph. Returns 0 if the character
 * has no glyph (caller should just advance the pen). */
int font_glyph_uv(const font_t *font, char c, float *u0, float *v0, float *u1, float *v1);

/* Horizontal pixel advance per character (including inter-glyph spacing)
 * at the given integer pixel scale. */
int font_glyph_advance(int scale);

/* Total rendered width in pixels of `text` at the given scale. */
int font_text_width(const char *text, int scale);

#endif
