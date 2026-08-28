#ifndef GCAL_COLORS_H
#define GCAL_COLORS_H

/* Maps a Google Calendar API Event.colorId (1-11) to RGB in 0..1.
 * color_id <= 0 or > 11 (unset, or a value the API adds later) yields
 * a fixed default color instead. */
void gcal_color_for_id(int color_id, float *r, float *g, float *b);

#endif
