#include "gcal_colors.h"
#include <stddef.h>

/* This is the Google Calendar API's "event" colorId palette (the
 * colors().get() response's `event` field, IDs 1-11) -- the lighter,
 * pastel table used to tint individual events. It is NOT the same as
 * the darker `calendar` field palette used for whole-calendar swatches
 * in the Calendar UI sidebar; the two are easy to conflate since both
 * are commonly (mis)quoted online as "the Google Calendar colors". */
typedef struct {
    unsigned char r, g, b;
} rgb8_t;

static const rgb8_t EVENT_COLORS[11] = {
    {0xA4, 0xBD, 0xFC}, /* 1 Lavender */
    {0x7A, 0xE7, 0xBF}, /* 2 Sage */
    {0xDB, 0xAD, 0xFF}, /* 3 Grape */
    {0xFF, 0x88, 0x7C}, /* 4 Flamingo */
    {0xFB, 0xD7, 0x5B}, /* 5 Banana */
    {0xFF, 0xB8, 0x78}, /* 6 Tangerine */
    {0x46, 0xD6, 0xDB}, /* 7 Peacock */
    {0xE1, 0xE1, 0xE1}, /* 8 Graphite */
    {0x54, 0x84, 0xED}, /* 9 Blueberry */
    {0x51, 0xB7, 0x49}, /* 10 Basil */
    {0xDC, 0x21, 0x27}, /* 11 Tomato */
};

static const rgb8_t DEFAULT_COLOR = {0x42, 0x85, 0xF4}; /* Google brand blue */

void gcal_color_for_id(int color_id, float *r, float *g, float *b) {
    rgb8_t c = (color_id >= 1 && color_id <= 11) ? EVENT_COLORS[color_id - 1] : DEFAULT_COLOR;
    *r = (float)c.r / 255.0f;
    *g = (float)c.g / 255.0f;
    *b = (float)c.b / 255.0f;
}
