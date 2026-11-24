#define _GNU_SOURCE
#include "canvas.h"
#include "lock.h"
#include <string.h>
#include <stdlib.h>

static void undo_push(SharedBoard *board, int x, int y,
                      char old_ch, uint8_t old_color, int slot)
{
    UndoEntry *e = &board->undo_buf[board->undo_head];
    e->x = x;
    e->y = y;
    e->old_ch = old_ch;
    e->old_color = old_color;
    e->user_slot = slot;
    board->undo_head = (board->undo_head + 1) % UNDO_RING_SZ;
    if (board->undo_count < UNDO_RING_SZ)
        board->undo_count++;
}

void canvas_draw(SharedBoard *board, int x, int y, char ch, uint8_t color)
{
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) return;

    lock_row_acquire(board, y);

    Cell *cell = &board->canvas[y][x];
    undo_push(board, x, y, cell->ch, cell->color, -1);
    cell->ch    = ch;
    cell->color = color;
    lock_notify(board);

    lock_row_release(board, y);
}

void canvas_clear(SharedBoard *board)
{
    lock_range_acquire(board, 0, CANVAS_H - 1);

    for (int r = 0; r < CANVAS_H; r++)
        for (int c = 0; c < CANVAS_W; c++) {
            board->canvas[r][c].ch    = ' ';
            board->canvas[r][c].color = 0;
        }
    lock_notify(board);

    lock_range_release(board, 0, CANVAS_H - 1);
}

void canvas_flood_fill(SharedBoard *board, int x, int y, char ch, uint8_t color)
{
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) return;

    lock_range_acquire(board, 0, CANVAS_H - 1);

    char    target_ch    = board->canvas[y][x].ch;
    uint8_t target_color = board->canvas[y][x].color;

    if (target_ch == ch && target_color == color) {
        lock_range_release(board, 0, CANVAS_H - 1);
        return;
    }

    /* Simple stack-based BFS flood fill */
    int  cap   = CANVAS_W * CANVAS_H;
    int *stack = malloc(cap * 2 * sizeof(int));
    if (!stack) {
        lock_range_release(board, 0, CANVAS_H - 1);
        return;
    }

    int top = 0;
    stack[top * 2]     = x;
    stack[top * 2 + 1] = y;
    top++;

    char *visited = calloc(CANVAS_W * CANVAS_H, 1);
    if (!visited) {
        free(stack);
        lock_range_release(board, 0, CANVAS_H - 1);
        return;
    }
    visited[y * CANVAS_W + x] = 1;

    while (top > 0) {
        top--;
        int cx = stack[top * 2];
        int cy = stack[top * 2 + 1];

        if (board->canvas[cy][cx].ch    != target_ch ||
            board->canvas[cy][cx].color != target_color)
            continue;

        undo_push(board, cx, cy,
                  board->canvas[cy][cx].ch,
                  board->canvas[cy][cx].color, -1);
        board->canvas[cy][cx].ch    = ch;
        board->canvas[cy][cx].color = color;

        int dx[] = {1, -1, 0,  0};
        int dy[] = {0,  0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d];
            int ny = cy + dy[d];
            if (nx >= 0 && nx < CANVAS_W && ny >= 0 && ny < CANVAS_H &&
                !visited[ny * CANVAS_W + nx]) {
                visited[ny * CANVAS_W + nx] = 1;
                stack[top * 2]     = nx;
                stack[top * 2 + 1] = ny;
                top++;
            }
        }
    }

    free(stack);
    free(visited);
    lock_notify(board);
    lock_range_release(board, 0, CANVAS_H - 1);
}

void canvas_undo(SharedBoard *board, int slot_index)
{
    (void)slot_index; 

    lock_range_acquire(board, 0, CANVAS_H - 1);

    if (board->undo_count > 0) {
        board->undo_head = (board->undo_head - 1 + UNDO_RING_SZ) % UNDO_RING_SZ;
        board->undo_count--;
        UndoEntry *e = &board->undo_buf[board->undo_head];
        if (e->x >= 0 && e->x < CANVAS_W && e->y >= 0 && e->y < CANVAS_H) {
            board->canvas[e->y][e->x].ch    = e->old_ch;
            board->canvas[e->y][e->x].color = e->old_color;
        }
        lock_notify(board);
    }

    lock_range_release(board, 0, CANVAS_H - 1);
}
