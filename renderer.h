#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>
#include <stdint.h>

void render_init(void);
void render_cleanup(void);

/* Server-side: render all dirty content lines with cursor positioning.
   If full_redraw, render all rows. Returns length written. */
int render_content_to_string(char *buf, int buf_size, bool full_redraw);

/* Server-side: render status bar with cursor positioning. Returns length written. */
int render_status_bar_to_string(char *buf, int buf_size, bool clock_update);

#endif
