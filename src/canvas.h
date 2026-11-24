#ifndef CANVAS_OPS_H
#define CANVAS_OPS_H

#include "shm.h"

/* Draw a single character at (x,y) with the given color.
   Records the previous cell in the undo buffer. */
void canvas_draw(SharedBoard *board, int x, int y, char ch, uint8_t color);

/* Clear the entire canvas to spaces / color 0. */
void canvas_clear(SharedBoard *board);

/* Flood fill from (x,y) with ch/color. */
void canvas_flood_fill(SharedBoard *board, int x, int y, char ch, uint8_t color);

/* Undo the last stroke recorded by user at slot_index. */
void canvas_undo(SharedBoard *board, int slot_index);

#endif /* CANVAS_OPS_H */
