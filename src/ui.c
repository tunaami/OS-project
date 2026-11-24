#define _GNU_SOURCE
#include "ui.h"
#include "canvas.h"
#include "lock.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>


#define COLOR_PAIR_FOR(c) ((c) > 0 && (c) <= 8 ? (c) : 1)
#define STATUS_ROW (CANVAS_H)

int ui_init(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);  
    curs_set(0);

    if (!has_colors()) {
        endwin();
        fprintf(stderr, "Terminal does not support colors\n");
        return -1;
    }

    start_color();
    use_default_colors();
    init_pair(1, COLOR_WHITE,   -1);
    init_pair(2, COLOR_RED,     -1);
    init_pair(3, COLOR_GREEN,   -1);
    init_pair(4, COLOR_YELLOW,  -1);
    init_pair(5, COLOR_BLUE,    -1);
    init_pair(6, COLOR_MAGENTA, -1);
    init_pair(7, COLOR_CYAN,    -1);
    init_pair(8, COLOR_WHITE,   -1);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    if (rows < CANVAS_H + 1 || cols < CANVAS_W) {
        endwin();
        fprintf(stderr,
            "Terminal too small: need at least %dx%d, got %dx%d\n",
            CANVAS_W, CANVAS_H + 1, cols, rows);
        return -1;
    }

    clear();
    return 0;
}

void ui_cleanup(void)
{
    endwin();
}

void ui_redraw(SharedBoard *board, int my_slot)
{
   
    for (int r = 0; r < CANVAS_H; r++) {
        for (int c = 0; c < CANVAS_W; c++) {
            Cell   *cell = &board->canvas[r][c];
            uint8_t cp   = COLOR_PAIR_FOR(cell->color);
            mvaddch(r, c, cell->ch | COLOR_PAIR(cp));
        }
    }

    for (int i = 0; i < MAX_USERS; i++) {
        if (!board->users[i].active) continue;
        int cx = board->users[i].cursor_x;
        int cy = board->users[i].cursor_y;
        uint8_t cp = COLOR_PAIR_FOR(board->users[i].color);

        if (i == my_slot) {
            /* Own cursor: reverse video */
            chtype existing = mvwinch(stdscr, cy, cx) & A_CHARTEXT;
            mvaddch(cy, cx, (existing ? existing : ' ') | COLOR_PAIR(cp) | A_REVERSE);
        } else {
            chtype existing = mvwinch(stdscr, cy, cx) & A_CHARTEXT;
            mvaddch(cy, cx, (existing ? existing : ' ') | COLOR_PAIR(cp) | A_BOLD | A_UNDERLINE);
        }
    }

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)rows;

    move(STATUS_ROW, 0);
    clrtoeol();

    char bar[256] = {0};
    int  off      = 0;
    off += snprintf(bar + off, sizeof(bar) - off, "Users: ");
    for (int i = 0; i < MAX_USERS; i++) {
        if (!board->users[i].active) continue;
        off += snprintf(bar + off, sizeof(bar) - off,
                        "[%s%s] ",
                        board->users[i].username,
                        (i == my_slot ? "*" : ""));
    }

    if (my_slot >= 0 && my_slot < MAX_USERS) {
        off += snprintf(bar + off, sizeof(bar) - off,
                        " | Color:%d | Lock:%s",
                        board->users[my_slot].color,
                        g_lock_mode == LOCK_GLOBAL  ? "global" :
                        g_lock_mode == LOCK_PER_ROW ? "per-row" : "NONE");
    }

    off += snprintf(bar + off, sizeof(bar) - off,
                    " | arrows/WASD=move  SPC=draw  1-8=color  c=clear  f=fill  u=undo  q=quit");

    mvaddnstr(STATUS_ROW, 0, bar, cols - 1);

    /* Second status line if room */
    if (rows > STATUS_ROW + 1) {
        move(STATUS_ROW + 1, 0);
        clrtoeol();
    }

    refresh();
}


void *thread_input(void *arg)
{
    ClientState *cs    = (ClientState *)arg;
    SharedBoard *board = cs->board;
    int          slot  = cs->slot;

    int pen_down = 0;
    int cx = 0, cy = 0;

    board->users[slot].cursor_x = cx;
    board->users[slot].cursor_y = cy;

    while (cs->running) {
        int ch = getch();
        if (ch == ERR) {
            usleep(16000);  
            continue;
        }

        int moved = 0;

        switch (ch) {
        /* Movement */
        case KEY_UP:    case 'w': case 'W': cy = (cy > 0            ? cy-1 : cy); moved=1; break;
        case KEY_DOWN:  case 's': case 'S': cy = (cy < CANVAS_H-1   ? cy+1 : cy); moved=1; break;
        case KEY_LEFT:  case 'a': case 'A': cx = (cx > 0            ? cx-1 : cx); moved=1; break;
        case KEY_RIGHT: case 'd': case 'D': cx = (cx < CANVAS_W-1   ? cx+1 : cx); moved=1; break;

        /* Pen toggle */
        case ' ':
            pen_down = !pen_down;
            if (pen_down)
                canvas_draw(board, cx, cy, '*', board->users[slot].color);
            break;

        /* Color selection 1-8 */
        case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8':
            board->users[slot].color = (uint8_t)(ch - '0');
            break;

        /* Clear */
        case 'c': case 'C':
            canvas_clear(board);
            break;

        /* Flood fill */
        case 'f': case 'F':
            canvas_flood_fill(board, cx, cy, '#', board->users[slot].color);
            break;

        /* Undo */
        case 'u': case 'U':
            canvas_undo(board, slot);
            break;

        /* Quit */
        case 'q': case 'Q':
            cs->running = 0;
            break;

        default:
            /* Any printable character: draw it */
            if (ch >= 32 && ch < 127) {
                canvas_draw(board, cx, cy, (char)ch, board->users[slot].color);
                moved = 1;
                cx = (cx < CANVAS_W - 1 ? cx + 1 : cx);
            }
            break;
        }

        if (moved) {
            /* Update cursor position in shared memory */
            lock_global_acquire(board);
            board->users[slot].cursor_x = cx;
            board->users[slot].cursor_y = cy;
            lock_notify(board);
            lock_global_release(board);
            /* Draw after releasing the global lock to avoid re-entrancy */
            if (pen_down)
                canvas_draw(board, cx, cy, '*', board->users[slot].color);
        }

        cs->needs_redraw = 1;
    }

    return NULL;
}

void *thread_listener(void *arg)
{
    ClientState *cs    = (ClientState *)arg;
    SharedBoard *board = cs->board;
    int          slot  = cs->slot;

    uint64_t last_ver = board->version;

    while (cs->running) {
        if (g_lock_mode != LOCK_NONE) {
            lock_global_acquire(board);
            last_ver = lock_wait_change(board, last_ver);
            lock_global_release(board);
        } else {
            while (cs->running && board->version == last_ver)
                usleep(16000);
            last_ver = board->version;
        }

        if (!cs->running) break;
        ui_redraw(board, slot);
    }

    return NULL;
}
